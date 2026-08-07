#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <windows.h>

#include "control_server.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstring>
#include <filesystem>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <system_error>

#include <ntl/wfp/management>

#include "controller_protocol.hpp"
#include "kernel_tls_service.hpp"
#include "managed_policy.hpp"

namespace crtsys::wfp_kernel_browser_https {
namespace {

namespace contract = wfp_kernel_browser_https_inspection;
namespace protocol = controller_protocol;

class pipe_handle {
public:
  explicit pipe_handle(HANDLE value) : value_(value) {}
  pipe_handle(const pipe_handle &) = delete;
  pipe_handle &operator=(const pipe_handle &) = delete;
  ~pipe_handle() {
    if (value_ != INVALID_HANDLE_VALUE)
      (void)::CloseHandle(value_);
  }
  HANDLE get() const noexcept { return value_; }

private:
  HANDLE value_ = INVALID_HANDLE_VALUE;
};

bool read_exact(HANDLE pipe, void *buffer, std::size_t size) {
  auto *next = static_cast<std::byte *>(buffer);
  while (size != 0) {
    DWORD read = 0;
    if (!::ReadFile(pipe, next, static_cast<DWORD>(size), &read, nullptr)) {
      const auto error = ::GetLastError();
      if (error == ERROR_BROKEN_PIPE)
        return false;
      throw std::system_error(error, std::system_category(),
                              "read kernel browser control pipe");
    }
    if (read == 0)
      return false;
    next += read;
    size -= read;
  }
  return true;
}

void write_exact(HANDLE pipe, const void *buffer, std::size_t size) {
  const auto *next = static_cast<const std::byte *>(buffer);
  while (size != 0) {
    DWORD written = 0;
    if (!::WriteFile(pipe, next, static_cast<DWORD>(size), &written,
                     nullptr))
      throw std::system_error(::GetLastError(), std::system_category(),
                              "write kernel browser control pipe");
    if (written == 0)
      throw std::runtime_error("kernel browser control pipe made no progress");
    next += written;
    size -= written;
  }
}

void arm_origin_security_rollback_test(HANDLE device) {
  DWORD bytes = 0;
  if (!::DeviceIoControl(
          device, contract::arm_origin_security_rollback_test_ioctl,
          nullptr, 0, nullptr, 0, &bytes, nullptr))
    throw std::system_error(
        ::GetLastError(), std::system_category(),
        "arm kernel browser origin security rollback test");
}

void configure_origin(HANDLE device,
                      const contract::origin_security_config &value) {
  const std::string_view server_name(value.server_name.data(),
                                     value.server_name_size);
  if (value.action == contract::origin_security_action::remove) {
    remove_origin_security(device, server_name);
    return;
  }
  if (value.action != contract::origin_security_action::install)
    throw std::invalid_argument("unknown origin security action");
  configure_origin_security(
      device, server_name, value.client_sha1_thumbprint,
      std::span(value.origin_leaf_der.data(), value.origin_leaf_der_size));
}

void set_error(protocol::response &reply, std::string_view message) noexcept {
  reply.failed = 1;
  const auto size = (std::min)(message.size(), reply.error.size() - 1);
  std::memcpy(reply.error.data(), message.data(), size);
  reply.error[size] = '\0';
}

} // namespace

int run_control_server(std::wstring_view pipe_name,
                       const std::filesystem::path &application_path) {
  if (!pipe_name.starts_with(L"\\\\.\\pipe\\") ||
      pipe_name.size() <= std::wstring_view(L"\\\\.\\pipe\\").size())
    throw std::invalid_argument("control pipe name is invalid");
  const auto application = std::filesystem::canonical(application_path);
  if (!std::filesystem::is_regular_file(application))
    throw std::invalid_argument("controlled application is not a regular file");
  const auto application_id =
      ntl::wfp::application_id::from_path(application.wstring());

  pipe_handle pipe(::CreateNamedPipeW(
      std::wstring(pipe_name).c_str(), PIPE_ACCESS_DUPLEX | FILE_FLAG_FIRST_PIPE_INSTANCE,
      PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT, 1,
      static_cast<DWORD>(sizeof(protocol::response)),
      static_cast<DWORD>(sizeof(protocol::request)), 0, nullptr));
  if (pipe.get() == INVALID_HANDLE_VALUE)
    throw std::system_error(::GetLastError(), std::system_category(),
                            "create kernel browser control pipe");
  if (!::ConnectNamedPipe(pipe.get(), nullptr) &&
      ::GetLastError() != ERROR_PIPE_CONNECTED)
    throw std::system_error(::GetLastError(), std::system_category(),
                            "connect kernel browser control pipe");

  device_handle device;
  std::optional<ntl::wfp::policy_session> active_policy;
  protocol::request command{};
  while (read_exact(pipe.get(), &command, sizeof(command))) {
    protocol::response reply{};
    reply.size = sizeof(reply);
    reply.code = command.code;
    bool stop = false;
    try {
      if (command.protocol_version != protocol::version ||
          command.size != sizeof(command))
        throw std::runtime_error("kernel browser control protocol mismatch");
      switch (command.code) {
      case protocol::operation::hello:
      case protocol::operation::query_service:
        reply.service = query_service(device.get());
        break;
      case protocol::operation::read_inspection:
        reply.inspection = read_inspection(device.get(), command.cursor);
        break;
      case protocol::operation::read_identity_request:
        reply.identity_request =
            read_identity_request(device.get(), command.cursor);
        break;
      case protocol::operation::query_quic_telemetry:
        reply.quic = query_quic_telemetry(device.get());
        break;
      case protocol::operation::configure_identity:
        configure_identity(device.get(), command.identity);
        break;
      case protocol::operation::configure_origin_security:
        configure_origin(device.get(), command.origin_security);
        break;
      case protocol::operation::arm_origin_security_rollback:
        arm_origin_security_rollback_test(device.get());
        break;
      case protocol::operation::install_tcp_policy: {
        active_policy.reset();
        auto policy = ntl::wfp::policy_session::ephemeral(
            L"crtsys kernel browser controlled TCP policy");
        const auto evidence = install_managed_tcp_policy(
            policy, application_id, query_service(device.get()),
            command.port_v4, command.port_v6);
        reply.tcp_policy = {evidence.application_id_hash,
                            evidence.filter_id_v4, evidence.filter_id_v6};
        active_policy.emplace(std::move(policy));
        break;
      }
      case protocol::operation::install_http3_policy: {
        active_policy.reset();
        auto policy = ntl::wfp::policy_session::ephemeral(
            L"crtsys kernel browser controlled HTTP/3 policy");
        const auto evidence = install_managed_http3_policy(
            policy, application_id, query_service(device.get()),
            command.port_v4);
        reply.http3_policy = {evidence.application_id_hash,
                              evidence.http3_filter_id_v4,
                              evidence.http3_filter_id_v6,
                              evidence.datagram_filter_id_v4,
                              evidence.datagram_filter_id_v6,
                              evidence.reverse_filter_id_v4,
                              evidence.reverse_filter_id_v6};
        active_policy.emplace(std::move(policy));
        break;
      }
      case protocol::operation::clear_policy:
        active_policy.reset();
        break;
      case protocol::operation::stop:
        active_policy.reset();
        stop = true;
        break;
      default:
        throw std::invalid_argument("unknown kernel browser control command");
      }
    } catch (const std::exception &error) {
      set_error(reply, error.what());
    } catch (...) {
      set_error(reply, "unknown kernel browser control failure");
    }
    write_exact(pipe.get(), &reply, sizeof(reply));
    if (stop && reply.failed == 0)
      break;
  }
  (void)::FlushFileBuffers(pipe.get());
  (void)::DisconnectNamedPipe(pipe.get());
  return 0;
}

} // namespace crtsys::wfp_kernel_browser_https
