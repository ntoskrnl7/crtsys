#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <windows.h>
#include <wincrypt.h>
#include <winioctl.h>

#include "controller.hpp"

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string_view>
#include <system_error>
#include <thread>
#include <utility>

#include <ntl/net/tls/certificate>
#include <ntl/wfp/all>

#include "controller_lifecycle.hpp"
#include "http3_inspection_contract.hpp"
#include "test_certificate.hpp"
#include "windows_support.hpp"

namespace crtsys::wfp_kernel_http3 {
namespace {

namespace contract = wfp_kernel_http3_inspection;
using namespace crtsys::wfp_sample;
using namespace std::chrono_literals;

class handle_owner {
public:
  explicit handle_owner(HANDLE value = INVALID_HANDLE_VALUE) noexcept
      : value_(value) {}
  handle_owner(const handle_owner &) = delete;
  handle_owner &operator=(const handle_owner &) = delete;
  ~handle_owner() {
    if (value_ != INVALID_HANDLE_VALUE)
      (void)::CloseHandle(value_);
  }
  HANDLE get() const noexcept { return value_; }

private:
  HANDLE value_;
};

class installed_certificate {
public:
  installed_certificate(PCCERT_CONTEXT certificate, const wchar_t *store) {
    store_ = ::CertOpenStore(
        CERT_STORE_PROV_SYSTEM_W, 0, 0,
        CERT_SYSTEM_STORE_LOCAL_MACHINE | CERT_STORE_OPEN_EXISTING_FLAG |
            CERT_STORE_MAXIMUM_ALLOWED_FLAG,
        store);
    if (!store_)
      throw_windows("CertOpenStore(kernel HTTP/3 controller)");
    if (!::CertAddCertificateContextToStore(
            store_, certificate, CERT_STORE_ADD_REPLACE_EXISTING, &stored_))
      throw_windows("CertAddCertificateContextToStore(kernel HTTP/3)");
    DWORD size = static_cast<DWORD>(thumbprint_.size());
    if (!::CertGetCertificateContextProperty(
            stored_, CERT_SHA1_HASH_PROP_ID, thumbprint_.data(), &size) ||
        size != thumbprint_.size())
      throw_windows("CertGetCertificateContextProperty(kernel HTTP/3)");
  }
  installed_certificate(const installed_certificate &) = delete;
  installed_certificate &operator=(const installed_certificate &) = delete;
  ~installed_certificate() {
    if (stored_)
      (void)::CertDeleteCertificateFromStore(stored_);
    if (store_)
      (void)::CertCloseStore(store_, 0);
  }
  const std::array<std::byte, contract::certificate_thumbprint_size> &
  thumbprint() const noexcept {
    return thumbprint_;
  }

private:
  HCERTSTORE store_ = nullptr;
  PCCERT_CONTEXT stored_ = nullptr;
  std::array<std::byte, contract::certificate_thumbprint_size> thumbprint_{};
};

void configure(HANDLE device, const installed_certificate &leaf) {
  contract::certificate_config input{leaf.thumbprint()};
  DWORD bytes = 0;
  if (!::DeviceIoControl(device, contract::configure_ioctl, &input,
                         sizeof(input), nullptr, 0, &bytes, nullptr))
    throw_windows("DeviceIoControl(configure kernel HTTP/3)");
}

contract::service_info query(HANDLE device) {
  contract::service_info result{};
  DWORD bytes = 0;
  if (!::DeviceIoControl(device, contract::query_ioctl, nullptr, 0, &result,
                         sizeof(result), &bytes, nullptr) ||
      bytes != sizeof(result))
    throw_windows("DeviceIoControl(query kernel HTTP/3)");
  return result;
}

contract::inspection_record capture(HANDLE device) {
  contract::inspection_record result{};
  DWORD bytes = 0;
  if (!::DeviceIoControl(device, contract::capture_ioctl, nullptr, 0, &result,
                         sizeof(result), &bytes, nullptr) ||
      bytes != sizeof(result))
    throw_windows("DeviceIoControl(capture kernel HTTP/3)");
  return result;
}

void install_policy(ntl::wfp::policy_session &session,
                    const ntl::wfp::application_id &application,
                    std::uint16_t port, bool unavailable) {
  session.install([&](ntl::wfp::policy_transaction &transaction) {
    const auto provider = transaction.add_provider(
        unavailable
            ? ntl::wfp::provider_spec{
                  contract::unavailable_provider_key,
                  L"crtsys kernel HTTP/3 unavailable provider",
                  L"Negative proof with deliberately unregistered callouts"}
            : ntl::wfp::provider_spec{
                  contract::provider_key,
                  L"crtsys kernel HTTP/3 provider",
                  L"App-scoped dual-stack UDP policy before kernel MsQuic"});
    const auto sublayer = transaction.add_sublayer(
        provider,
        unavailable
            ? ntl::wfp::sublayer_spec{
                  contract::unavailable_sublayer_key,
                  L"crtsys kernel HTTP/3 unavailable sublayer",
                  L"Fail closed when the exact callout key is absent", 0x7641}
            : ntl::wfp::sublayer_spec{
                  contract::sublayer_key,
                  L"crtsys kernel HTTP/3 sublayer",
                  L"Typed ALE policy before kernel QUIC termination", 0x7640});

    if (unavailable) {
      const auto callout_v4 = transaction.add_callout<contract::layer_v4>(
          provider, {contract::unavailable_callout_key_v4,
                     L"Unavailable kernel HTTP/3 IPv4 callout", L""});
      const auto callout_v6 = transaction.add_callout<contract::layer_v6>(
          provider, {contract::unavailable_callout_key_v6,
                     L"Unavailable kernel HTTP/3 IPv6 callout", L""});
      ntl::wfp::filter_builder<contract::layer_v4> v4(
          contract::unavailable_filter_key_v4,
          L"Fail closed for controlled IPv4 HTTP/3",
          ntl::wfp::callout_unavailable::block);
      v4.application_equal(application).protocol_equal(IPPROTO_UDP)
          .remote_port_equal(port);
      transaction.add_filter(sublayer, callout_v4, v4);
      ntl::wfp::filter_builder<contract::layer_v6> v6(
          contract::unavailable_filter_key_v6,
          L"Fail closed for controlled IPv6 HTTP/3",
          ntl::wfp::callout_unavailable::block);
      v6.application_equal(application).protocol_equal(IPPROTO_UDP)
          .remote_port_equal(port);
      transaction.add_filter(sublayer, callout_v6, v6);
      return;
    }

    const auto callout_v4 = transaction.add_callout<contract::layer_v4>(
        provider, {contract::callout_key_v4, L"Kernel HTTP/3 IPv4 gate", L""});
    const auto callout_v6 = transaction.add_callout<contract::layer_v6>(
        provider, {contract::callout_key_v6, L"Kernel HTTP/3 IPv6 gate", L""});
    ntl::wfp::filter_builder<contract::layer_v4> v4(
        contract::filter_key_v4, L"Inspect controlled kernel HTTP/3 IPv4",
        ntl::wfp::callout_unavailable::block);
    v4.application_equal(application).protocol_equal(IPPROTO_UDP)
        .remote_port_equal(port);
    transaction.add_filter(sublayer, callout_v4, v4);
    ntl::wfp::filter_builder<contract::layer_v6> v6(
        contract::filter_key_v6, L"Inspect controlled kernel HTTP/3 IPv6",
        ntl::wfp::callout_unavailable::block);
    v6.application_equal(application).protocol_equal(IPPROTO_UDP)
        .remote_port_equal(port);
    transaction.add_filter(sublayer, callout_v6, v6);
  });
}

std::optional<ntl::wfp::policy_session> make_policy(
    const ntl::wfp::application_id &application, std::uint16_t port,
    bool unavailable) {
  auto session = ntl::wfp::policy_session::ephemeral(
      unavailable ? L"crtsys kernel HTTP/3 unavailable-callout proof"
                  : L"crtsys kernel HTTP/3 controller");
  install_policy(session, application, port, unavailable);
  return std::optional<ntl::wfp::policy_session>(std::move(session));
}

void write_capture(const std::filesystem::path &directory,
                   const contract::inspection_record &record) {
  const auto write = [](const std::filesystem::path &path,
                        const std::byte *data, std::size_t size) {
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output)
      throw std::runtime_error("cannot create kernel HTTP/3 capture file");
    output.write(reinterpret_cast<const char *>(data),
                 static_cast<std::streamsize>(size));
    if (!output)
      throw std::runtime_error("cannot write kernel HTTP/3 capture file");
  };
  write(directory / L"request.txt", record.request.data(),
        record.request_size);
  write(directory / L"response.html", record.response.data(),
        record.response_size);
}

std::string format_stats(std::string_view state,
                         const contract::service_info &value,
                         const contract::inspection_record &record,
                         bool direct_unchanged,
                         bool unavailable_unchanged,
                         bool restored) {
  std::ostringstream out;
  out << "state=" << state << '\n'
      << "port=" << value.port << '\n'
      << "ready=" << value.ready << '\n'
      << "wfp_ipv4=" << value.wfp_ipv4 << '\n'
      << "wfp_ipv6=" << value.wfp_ipv6 << '\n'
      << "accepted=" << value.accepted << '\n'
      << "permitted=" << value.permitted << '\n'
      << "blocked=" << value.blocked << '\n'
      << "failed=" << value.failed << '\n'
      << "qpack_resumed=" << value.qpack_resumed << '\n'
      << "gzip_responses=" << value.gzip_responses << '\n'
      << "deflate_responses=" << value.deflate_responses << '\n'
      << "brotli_responses=" << value.brotli_responses << '\n'
      << "webtransport_sessions=" << value.webtransport_sessions << '\n'
      << "webtransport_bidirectional=" << value.webtransport_bidirectional << '\n'
      << "webtransport_unidirectional=" << value.webtransport_unidirectional << '\n'
      << "webtransport_datagrams=" << value.webtransport_datagrams << '\n'
      << "webtransport_capsules=" << value.webtransport_capsules << '\n'
      << "webtransport_resets=" << value.webtransport_resets << '\n'
      << "active_connections=" << value.active_connections << '\n'
      << "peak_connections=" << value.peak_connections << '\n'
      << "reaped_connections=" << value.reaped_connections << '\n'
      << "capture_sequence=" << record.sequence << '\n'
      << "capture_status=" << record.status << '\n'
      << "capture_request_size=" << record.request_size << '\n'
      << "capture_response_size=" << record.response_size << '\n'
      << "direct_counter_unchanged=" << (direct_unchanged ? 1 : 0) << '\n'
      << "unavailable_origin_unchanged=" << (unavailable_unchanged ? 1 : 0)
      << '\n'
      << "restored=" << (restored ? 1 : 0) << '\n';
  return out.str();
}

bool same_gate_counters(const contract::service_info &left,
                        const contract::service_info &right) noexcept {
  return left.wfp_ipv4 == right.wfp_ipv4 &&
         left.wfp_ipv6 == right.wfp_ipv6;
}

void remove_command(const controller_lifecycle &lifecycle,
                    std::wstring_view name) noexcept {
  std::error_code ignored;
  (void)std::filesystem::remove(
      lifecycle.directory() / std::filesystem::path(name), ignored);
}

} // namespace

int run_controller(const std::filesystem::path &controlled_application,
                   const std::filesystem::path &ipc_directory) {
  if (!std::filesystem::is_regular_file(controlled_application))
    throw std::invalid_argument("controlled application is not a file");
  winsock_session winsock;
  controller_lifecycle lifecycle(ipc_directory);
  for (const auto *name : {L"snapshot.request", L"snapshot.ready",
                           L"direct.request", L"direct.ready",
                           L"direct-done.request", L"direct-done.ready",
                           L"unavailable.request", L"unavailable.ready",
                           L"unavailable-done.request",
                           L"unavailable-done.ready", L"restore.ready"})
    remove_command(lifecycle, name);

  handle_owner device(::CreateFileW(
      contract::user_device_path, GENERIC_READ | GENERIC_WRITE,
      FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr));
  if (device.get() == INVALID_HANDLE_VALUE)
    throw_windows("open kernel HTTP/3 inspection device");

  ephemeral_certificate authority(true);
  installed_certificate root(authority.get(), L"ROOT");
  ntl::net::windows_tls_certificate_issuer issuer(
      authority.get(), {.key_name_prefix = L"crtsys-kernel-http3",
                        .rsa_bits = 2048,
                        .validity_days = 1,
                        .machine_keys = true});
  auto leaf = issuer.issue(L"localhost");
  installed_certificate installed_leaf(leaf.borrowed_certificate(), L"MY");
  configure(device.get(), installed_leaf);

  const auto initial = query(device.get());
  if (!initial.ready || !initial.port)
    throw std::runtime_error("kernel HTTP/3 listener is not ready");
  const auto application =
      ntl::wfp::application_id::from_path(controlled_application.wstring());
  auto policy = make_policy(application, initial.port, false);
  auto baseline = initial;
  bool direct_unchanged = false;
  bool unavailable_unchanged = false;
  bool restored = false;
  auto record = capture(device.get());
  lifecycle.publish_ready(format_stats("ready", initial, record, false,
                                       false, false));

  for (;;) {
    if (lifecycle.command_exists(L"stop.request"))
      break;
    if (lifecycle.command_exists(L"snapshot.request")) {
      const auto current = query(device.get());
      record = capture(device.get());
      write_capture(lifecycle.directory(), record);
      lifecycle.publish_stats(format_stats("snapshot", current, record,
                                           direct_unchanged,
                                           unavailable_unchanged, restored));
      remove_command(lifecycle, L"snapshot.request");
      lifecycle.acknowledge(L"snapshot.ready");
    }
    if (lifecycle.command_exists(L"direct.request")) {
      policy.reset();
      baseline = query(device.get());
      remove_command(lifecycle, L"direct.request");
      lifecycle.acknowledge(L"direct.ready");
    }
    if (lifecycle.command_exists(L"direct-done.request")) {
      const auto current = query(device.get());
      direct_unchanged = same_gate_counters(baseline, current);
      if (!direct_unchanged)
        throw std::runtime_error(
            "removed HTTP/3 policy still changed WFP gate counters");
      policy = make_policy(application, initial.port, false);
      remove_command(lifecycle, L"direct-done.request");
      lifecycle.acknowledge(L"direct-done.ready");
    }
    if (lifecycle.command_exists(L"unavailable.request")) {
      policy.reset();
      baseline = query(device.get());
      policy = make_policy(application, initial.port, true);
      remove_command(lifecycle, L"unavailable.request");
      lifecycle.acknowledge(L"unavailable.ready");
    }
    if (lifecycle.command_exists(L"unavailable-done.request")) {
      const auto current = query(device.get());
      unavailable_unchanged = current.accepted == baseline.accepted &&
                              same_gate_counters(baseline, current);
      if (!unavailable_unchanged)
        throw std::runtime_error(
            "unavailable HTTP/3 callout allowed origin access");
      policy.reset();
      policy = make_policy(application, initial.port, false);
      restored = true;
      remove_command(lifecycle, L"unavailable-done.request");
      lifecycle.acknowledge(L"unavailable-done.ready");
      lifecycle.acknowledge(L"restore.ready");
    }
    std::this_thread::sleep_for(25ms);
  }

  const auto final = query(device.get());
  record = capture(device.get());
  write_capture(lifecycle.directory(), record);
  lifecycle.publish_stats(format_stats("stopped", final, record,
                                       direct_unchanged,
                                       unavailable_unchanged, restored));
  policy.reset();
  return 0;
}

} // namespace crtsys::wfp_kernel_http3
