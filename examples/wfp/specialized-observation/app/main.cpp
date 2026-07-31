#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <winioctl.h>
#include <winsock2.h>
#include <ws2tcpip.h>

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>
#include <system_error>
#include <thread>
#include <utility>

#include <ntl/wfp/all>

#include "specialized_observation_contract.hpp"

namespace {

class handle_owner {
public:
  explicit handle_owner(HANDLE value = INVALID_HANDLE_VALUE) noexcept
      : value_(value) {}
  handle_owner(const handle_owner &) = delete;
  handle_owner &operator=(const handle_owner &) = delete;
  ~handle_owner() {
    if (value_ != INVALID_HANDLE_VALUE)
      CloseHandle(value_);
  }
  HANDLE get() const noexcept { return value_; }

private:
  HANDLE value_;
};

class socket_owner {
public:
  explicit socket_owner(SOCKET value = INVALID_SOCKET) noexcept
      : value_(value) {}
  socket_owner(const socket_owner &) = delete;
  socket_owner &operator=(const socket_owner &) = delete;
  socket_owner(socket_owner &&other) noexcept
      : value_(std::exchange(other.value_, INVALID_SOCKET)) {}
  ~socket_owner() {
    if (value_ != INVALID_SOCKET)
      closesocket(value_);
  }
  SOCKET get() const noexcept { return value_; }

private:
  SOCKET value_;
};

class winsock_session {
public:
  winsock_session() {
    WSADATA data{};
    const int result = WSAStartup(MAKEWORD(2, 2), &data);
    if (result)
      throw std::system_error(
          result, std::system_category(), "WSAStartup");
  }
  ~winsock_session() { WSACleanup(); }
};

template <class Layer>
void add_observer(
    ntl::wfp::policy_transaction &transaction,
    ntl::wfp::provider_ref provider,
    ntl::wfp::sublayer_ref sublayer,
    ntl::wfp::callout_key<Layer> callout_key,
    ntl::wfp::filter_key<Layer> filter_key,
    const wchar_t *name) {
  const auto callout = transaction.add_callout<Layer>(
      provider, {callout_key, name, L"Specialized WFP runtime observation"});
  ntl::wfp::inspection_filter_builder<Layer> filter(
      filter_key, std::wstring(name) + L" filter");
  transaction.add_inspection_filter(sublayer, callout, filter);
}

template <class Layer>
void add_endpoint_observer(
    ntl::wfp::policy_transaction &transaction,
    ntl::wfp::provider_ref provider,
    ntl::wfp::sublayer_ref sublayer,
    ntl::wfp::callout_key<Layer> callout_key,
    ntl::wfp::filter_key<Layer> filter_key,
    const ntl::wfp::application_id &application,
    const wchar_t *name) {
  const auto callout = transaction.add_callout<Layer>(
      provider, {callout_key, name, L"Endpoint closure observation"});
  ntl::wfp::inspection_filter_builder<Layer> filter(
      filter_key, std::wstring(name) + L" filter");
  filter.application_equal(application).protocol_equal(IPPROTO_TCP);
  transaction.add_inspection_filter(sublayer, callout, filter);
}

void install_policy(ntl::wfp::policy_session &session) {
  const auto application = ntl::wfp::application_id::current_process();
  session.install([&](ntl::wfp::policy_transaction &transaction) {
    const auto provider = transaction.add_provider(
        {wfp_specialized_observation::provider_key,
         L"crtsys NTL WFP specialized observation provider",
         L"Runtime capability and Driver Verifier fixture"});
    const auto sublayer = transaction.add_sublayer(
        provider,
        {wfp_specialized_observation::sublayer_key,
         L"crtsys NTL WFP specialized observation sublayer",
         L"Inspection-only specialized-layer filters", 0x7060});

    add_endpoint_observer<wfp_specialized_observation::endpoint_v4>(
        transaction, provider, sublayer,
        wfp_specialized_observation::endpoint_v4_callout_key,
        wfp_specialized_observation::endpoint_v4_filter_key,
        application, L"Endpoint closure IPv4");
    add_endpoint_observer<wfp_specialized_observation::endpoint_v6>(
        transaction, provider, sublayer,
        wfp_specialized_observation::endpoint_v6_callout_key,
        wfp_specialized_observation::endpoint_v6_filter_key,
        application, L"Endpoint closure IPv6");

#define NTL_ADD_SPECIALIZED(name, layer, title)                                 \
  add_observer<wfp_specialized_observation::layer>(                             \
      transaction, provider, sublayer,                                          \
      wfp_specialized_observation::name##_callout_key,                          \
      wfp_specialized_observation::name##_filter_key, title)

    NTL_ADD_SPECIALIZED(mac_in, mac_in, L"Inbound MAC frame");
    NTL_ADD_SPECIALIZED(mac_out, mac_out, L"Outbound MAC frame");
    NTL_ADD_SPECIALIZED(vswitch_in, vswitch_in, L"Ingress vSwitch frame");
    NTL_ADD_SPECIALIZED(vswitch_out, vswitch_out, L"Egress vSwitch frame");

#undef NTL_ADD_SPECIALIZED
  });
}

wfp_specialized_observation::observation_stats query_stats(HANDLE device) {
  wfp_specialized_observation::observation_stats result{};
  DWORD returned = 0;
  if (!DeviceIoControl(
          device, wfp_specialized_observation::query_stats_ioctl,
          nullptr, 0, &result, sizeof(result), &returned, nullptr) ||
      returned != sizeof(result))
    throw std::system_error(
        GetLastError(), std::system_category(), "DeviceIoControl");
  if (result.version != 1)
    throw std::runtime_error(
        "specialized-observation ABI version mismatch");
  return result;
}

void exchange_loopback(int family) {
  socket_owner listener(socket(family, SOCK_STREAM, IPPROTO_TCP));
  if (listener.get() == INVALID_SOCKET)
    throw std::system_error(
        WSAGetLastError(), std::system_category(), "socket(listener)");

  sockaddr_storage storage{};
  int address_size = 0;
  if (family == AF_INET) {
    auto &address = reinterpret_cast<sockaddr_in &>(storage);
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    address_size = sizeof(address);
  } else {
    DWORD v6_only = 1;
    if (setsockopt(listener.get(), IPPROTO_IPV6, IPV6_V6ONLY,
                   reinterpret_cast<const char *>(&v6_only),
                   sizeof(v6_only)) == SOCKET_ERROR)
      throw std::system_error(
          WSAGetLastError(), std::system_category(), "IPV6_V6ONLY");
    auto &address = reinterpret_cast<sockaddr_in6 &>(storage);
    address.sin6_family = AF_INET6;
    address.sin6_addr = in6addr_loopback;
    address_size = sizeof(address);
  }
  if (bind(listener.get(), reinterpret_cast<sockaddr *>(&storage),
           address_size) == SOCKET_ERROR ||
      listen(listener.get(), 1) == SOCKET_ERROR)
    throw std::system_error(
        WSAGetLastError(), std::system_category(), "bind/listen");
  int actual_size = sizeof(storage);
  if (getsockname(listener.get(), reinterpret_cast<sockaddr *>(&storage),
                  &actual_size) == SOCKET_ERROR)
    throw std::system_error(
        WSAGetLastError(), std::system_category(), "getsockname");

  std::exception_ptr server_failure;
  std::thread server([&] {
    try {
      socket_owner accepted(accept(listener.get(), nullptr, nullptr));
      if (accepted.get() == INVALID_SOCKET)
        throw std::system_error(
            WSAGetLastError(), std::system_category(), "accept");
      char byte = 0;
      if (recv(accepted.get(), &byte, 1, 0) != 1 || byte != 'x')
        throw std::runtime_error("loopback exchange payload mismatch");
    } catch (...) {
      server_failure = std::current_exception();
    }
  });

  socket_owner client(socket(family, SOCK_STREAM, IPPROTO_TCP));
  if (client.get() == INVALID_SOCKET ||
      connect(client.get(), reinterpret_cast<sockaddr *>(&storage),
              address_size) == SOCKET_ERROR ||
      send(client.get(), "x", 1, 0) != 1) {
    const auto error = WSAGetLastError();
    server.join();
    throw std::system_error(
        error, std::system_category(), "connect/send");
  }
  shutdown(client.get(), SD_BOTH);
  server.join();
  if (server_failure)
    std::rethrow_exception(server_failure);
}

template <class Layer>
void require_layer(ntl::wfp::policy_session &session,
                   const char *name) {
  const auto layer = session.inspect_layer<Layer>();
  if (!layer)
    throw std::runtime_error(
        std::string("required WFP layer is unavailable: ") + name);
  if (layer->layer_id == 0)
    throw std::runtime_error(
        std::string("WFP layer has no runtime identifier: ") + name);
}

} // namespace

int wmain() {
  try {
    winsock_session winsock;
    handle_owner device(CreateFileW(
        wfp_specialized_observation::user_device_path, GENERIC_READ,
        FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL, nullptr));
    if (device.get() == INVALID_HANDLE_VALUE)
      throw std::system_error(
          GetLastError(), std::system_category(), "CreateFileW");

    auto policy = ntl::wfp::policy_session::ephemeral(
        L"crtsys NTL WFP specialized observation");
    require_layer<wfp_specialized_observation::endpoint_v4>(
        policy, "ALE_ENDPOINT_CLOSURE_V4");
    require_layer<wfp_specialized_observation::endpoint_v6>(
        policy, "ALE_ENDPOINT_CLOSURE_V6");
    require_layer<wfp_specialized_observation::mac_in>(
        policy, "INBOUND_MAC_FRAME_ETHERNET");
    require_layer<wfp_specialized_observation::mac_out>(
        policy, "OUTBOUND_MAC_FRAME_ETHERNET");
    require_layer<wfp_specialized_observation::vswitch_in>(
        policy, "INGRESS_VSWITCH_ETHERNET");
    require_layer<wfp_specialized_observation::vswitch_out>(
        policy, "EGRESS_VSWITCH_ETHERNET");
    require_layer<wfp_specialized_observation::fast_in>(
        policy, "INBOUND_TRANSPORT_FAST");
    require_layer<wfp_specialized_observation::fast_out>(
        policy, "OUTBOUND_TRANSPORT_FAST");
    require_layer<wfp_specialized_observation::ipsec_v4>(
        policy, "IPSEC_V4");
    require_layer<wfp_specialized_observation::ipsec_v6>(
        policy, "IPSEC_V6");

    const auto before = query_stats(device.get());
    if (before.registered_mask !=
        wfp_specialized_observation::all_layers_mask)
      throw std::runtime_error(
          "the driver did not register every specialized callout");

    install_policy(policy);
    exchange_loopback(AF_INET);
    exchange_loopback(AF_INET6);

    auto after = query_stats(device.get());
    constexpr auto endpoint4 = static_cast<std::size_t>(
        wfp_specialized_observation::counter::endpoint_v4);
    constexpr auto endpoint6 = static_cast<std::size_t>(
        wfp_specialized_observation::counter::endpoint_v6);
    for (unsigned attempt = 0;
         attempt != 100 &&
         (after.indications[endpoint4] <= before.indications[endpoint4] ||
          after.indications[endpoint6] <= before.indications[endpoint6]);
         ++attempt) {
      std::this_thread::sleep_for(std::chrono::milliseconds(20));
      after = query_stats(device.get());
    }
    if (after.indications[endpoint4] <= before.indications[endpoint4] ||
        after.indications[endpoint6] <= before.indications[endpoint6])
      throw std::runtime_error(
          "dual-stack endpoint-closure callbacks were not observed");

    std::uint32_t exercised_mask = 0;
    for (std::size_t index = 0; index != after.indications.size(); ++index) {
      if (after.indications[index] > before.indications[index])
        exercised_mask |= 1u << index;
    }
    std::wcout
        << L"NTL WFP specialized-observation ok: registered-mask=0x"
        << std::hex << after.registered_mask
        << L", exercised-mask=0x" << exercised_mask
        << std::dec
        << L", endpoint-v4="
        << after.indications[endpoint4] - before.indications[endpoint4]
        << L", endpoint-v6="
        << after.indications[endpoint6] - before.indications[endpoint6]
        << L", optional-zero=capability-not-exercised"
        << L", fast-layers=introspection-only"
        << L", ipsec-layers=management-only\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "specialized-observation failed: "
              << error.what() << '\n';
    return 1;
  }
}
