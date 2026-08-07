#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <wincrypt.h>
#include <winioctl.h>

#include <array>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <span>
#include <string_view>
#include <thread>
#include <tuple>
#include <vector>

#include <ntl/handle>
#include <ntl/net/grpc/framing>
#include <ntl/net/offload/protocol>
#include <ntl/net/tls/certificate>
#include <ntl/net/tls/stream>
#include <ntl/net/user/task>

#include "contract.hpp"
#include "test_certificate.hpp"

namespace {

constexpr std::byte byte(char value) noexcept {
  return static_cast<std::byte>(static_cast<unsigned char>(value));
}

void append_u16(std::vector<std::byte> &output, std::size_t value) {
  output.push_back(static_cast<std::byte>((value >> 8) & 0xffu));
  output.push_back(static_cast<std::byte>(value & 0xffu));
}

std::vector<std::byte> make_client_hello() {
  constexpr std::string_view host = "kernel.example";
  std::vector<std::byte> extensions;
  append_u16(extensions, 0);
  append_u16(extensions, 2 + 1 + 2 + host.size());
  append_u16(extensions, 1 + 2 + host.size());
  extensions.push_back(std::byte{0});
  append_u16(extensions, host.size());
  for (const char value : host)
    extensions.push_back(byte(value));
  append_u16(extensions, 16);
  append_u16(extensions, 5);
  append_u16(extensions, 3);
  extensions.push_back(std::byte{2});
  extensions.push_back(byte('h'));
  extensions.push_back(byte('2'));

  std::vector<std::byte> body{std::byte{3}, std::byte{3}};
  body.resize(body.size() + 32);
  body.push_back(std::byte{0});
  append_u16(body, 2);
  body.push_back(std::byte{0x13});
  body.push_back(std::byte{0x01});
  body.push_back(std::byte{1});
  body.push_back(std::byte{0});
  append_u16(body, extensions.size());
  body.insert(body.end(), extensions.begin(), extensions.end());

  std::vector<std::byte> wire{std::byte{22}, std::byte{3}, std::byte{1}};
  append_u16(wire, body.size() + 4);
  wire.push_back(std::byte{1});
  wire.push_back(static_cast<std::byte>((body.size() >> 16) & 0xffu));
  wire.push_back(static_cast<std::byte>((body.size() >> 8) & 0xffu));
  wire.push_back(static_cast<std::byte>(body.size() & 0xffu));
  wire.insert(wire.end(), body.begin(), body.end());
  return wire;
}

bool invoke(ntl::unique_handle &device,
            ntl_net_kernel_sample::inspect_request &request,
            ntl_net_kernel_sample::inspect_reply &reply) {
  DWORD returned = 0;
  return DeviceIoControl(
             device.get(), ntl_net_kernel_sample::inspect_ioctl_code, &request,
             sizeof(request), &reply, sizeof(reply), &returned, nullptr) &&
         returned == sizeof(reply) && NT_SUCCESS(reply.parse_status);
}

ntl_net_kernel_sample::inspect_request
request_for(ntl_net_kernel_sample::protocol protocol,
            std::span<const std::byte> wire) {
  ntl_net_kernel_sample::inspect_request request{};
  request.kind = protocol;
  request.size = static_cast<std::uint32_t>(wire.size());
  std::memcpy(request.wire.data(), wire.data(), wire.size());
  return request;
}

bool run_async_stream_contract(ntl::unique_handle &device) {
  constexpr std::array<std::byte, 1> wire{std::byte{1}};
  auto request = request_for(
      ntl_net_kernel_sample::protocol::async_stream_state_machine, wire);
  ntl_net_kernel_sample::inspect_reply reply{};
  const bool invoked = invoke(device, request, reply);
  const bool valid = invoked && reply.field_count == 9 &&
                     reply.content_size == 96 * 1024 + 41 &&
                     reply.transformed[0] == std::byte{64} &&
                     reply.transformed[1] == std::byte{2} &&
                     reply.transformed[2] == std::byte{48} &&
                     reply.transformed[3] == std::byte{2} &&
                     reply.transformed[4] == std::byte{1} &&
                     reply.transformed[5] == std::byte{1} &&
                     (reply.flags &
                      ntl_net_kernel_sample::result_flag::
                          async_stream_serialized) != 0;
  if (!valid) {
    std::fprintf(stderr,
                 "async stream contract failed: invoked=%d "
                 "status=0x%08lx size=%lu fields=%lu flags=0x%08lx "
                 "steps=%u,%u,%u,%u,%u,%u,%u\n",
                 invoked ? 1 : 0,
                 static_cast<unsigned long>(reply.parse_status),
                 static_cast<unsigned long>(reply.content_size),
                 static_cast<unsigned long>(reply.field_count),
                 static_cast<unsigned long>(reply.flags),
                 std::to_integer<unsigned int>(reply.transformed[0]),
                 std::to_integer<unsigned int>(reply.transformed[1]),
                 std::to_integer<unsigned int>(reply.transformed[2]),
                 std::to_integer<unsigned int>(reply.transformed[3]),
                 std::to_integer<unsigned int>(reply.transformed[4]),
                 std::to_integer<unsigned int>(reply.transformed[5]),
                 std::to_integer<unsigned int>(reply.transformed[6]));
  }
  return valid;
}

ntl::net::user::task<bool>
run_kernel_tls_server(ntl::net::tls_stream &stream, std::string_view expected) {
  co_await stream.handshake_server(
      {.application_protocols = {"h2"}, .require_application_protocol = true});
  if (stream.negotiated_application_protocol() != "h2")
    co_return false;
  std::vector<std::byte> received(expected.size());
  std::size_t offset = 0;
  while (offset != received.size()) {
    const std::size_t count = co_await stream.read_some_borrowed(
        std::span<std::byte>(received).subspan(offset));
    if (count == 0)
      co_return false;
    offset += count;
  }
  if (std::memcmp(received.data(), expected.data(), expected.size()) != 0)
    co_return false;
  if (co_await stream.write_all(received) != received.size())
    co_return false;
  // Let the kernel start two concurrent close() calls while our authenticated
  // close_notify is still outstanding. Both calls must join one close state.
  std::this_thread::sleep_for(std::chrono::milliseconds(100));
  co_await stream.shutdown();
  std::array<std::byte, 1> end_probe{};
  if (co_await stream.read_some_borrowed(end_probe) != 0 ||
      !stream.received_close_notify())
    co_return false;
  co_return true;
}

} // namespace

int wmain(int argc, wchar_t **argv) {
  wchar_t path[128]{};
  std::swprintf(path, std::size(path), L"\\\\.\\%ls",
                ntl_net_kernel_sample::device_name);
  ntl::unique_handle device{CreateFileW(path, GENERIC_READ | GENERIC_WRITE, 0,
                                        nullptr, OPEN_EXISTING,
                                        FILE_ATTRIBUTE_NORMAL, nullptr)};
  if (!device) {
    std::fwprintf(stderr, L"CreateFileW failed: %lu\n", GetLastError());
    return 1;
  }

  if (argc == 2 && std::wstring_view(argv[1]) == L"--async-stream-only")
    return run_async_stream_contract(device) ? 0 : 24;

  constexpr std::array<std::byte, 3> grpc_payload{
      std::byte{'n'}, std::byte{'t'}, std::byte{'l'}};
  std::array<std::byte, 16> grpc_wire{};
  const auto grpc_size = ntl::net::grpc::encode_message_to(
      grpc_wire, grpc_payload, false, grpc_payload.size());
  if (!grpc_size)
    return 2;
  auto grpc =
      request_for(ntl_net_kernel_sample::protocol::grpc,
                  std::span<const std::byte>(grpc_wire).first(*grpc_size));
  ntl_net_kernel_sample::inspect_reply reply{};
  if (!invoke(device, grpc, reply) || reply.content_size != 3)
    return 3;

  constexpr std::string_view http1_wire =
      "GET / HTTP/1.1\r\nHost: kernel.example\r\n\r\n";
  auto http1 = request_for(
      ntl_net_kernel_sample::protocol::http1,
      std::as_bytes(std::span(http1_wire.data(), http1_wire.size())));
  if (!invoke(device, http1, reply) || reply.content_size != http1_wire.size())
    return 10;

  constexpr std::array<std::byte, 12> http2_wire{
      std::byte{0}, std::byte{0}, std::byte{3}, std::byte{0},
      std::byte{1}, std::byte{0}, std::byte{0}, std::byte{0},
      std::byte{1}, byte('n'),    byte('t'),    byte('l')};
  auto http2 = request_for(ntl_net_kernel_sample::protocol::http2, http2_wire);
  if (!invoke(device, http2, reply) || reply.content_size != 3 ||
      reply.field_count != 1)
    return 11;

  constexpr std::array<std::byte, 5> http3_wire{
      std::byte{0}, std::byte{3}, byte('n'), byte('t'), byte('l')};
  auto http3 = request_for(ntl_net_kernel_sample::protocol::http3, http3_wire);
  if (!invoke(device, http3, reply) || reply.content_size != 3)
    return 12;

  constexpr std::array<std::byte, 5> webtransport_wire{
      std::byte{0x40}, std::byte{0x41}, std::byte{0}, byte('w'), byte('t')};
  auto webtransport = request_for(ntl_net_kernel_sample::protocol::webtransport,
                                  webtransport_wire);
  if (!invoke(device, webtransport, reply) || reply.content_size != 2 ||
      reply.field_count != 0)
    return 13;

  constexpr std::array<std::byte, 1> offload_wire{byte('x')};
  auto offload = request_for(ntl_net_kernel_sample::protocol::offload_contract,
                             offload_wire);
  if (!invoke(device, offload, reply) ||
      reply.content_size != sizeof(ntl::net::offload::request_header) ||
      reply.field_count !=
          static_cast<std::uint32_t>(ntl::net::inspection::verdict::block) ||
      (reply.flags & ntl_net_kernel_sample::result_flag::offloaded) == 0)
    return 14;

  constexpr std::string_view codec_text =
      "bounded kernel gzip and Brotli share the NTL codec contract";
  const auto codec_wire =
      std::as_bytes(std::span(codec_text.data(), codec_text.size()));
  for (const auto kind : {ntl_net_kernel_sample::protocol::codec_gzip,
                          ntl_net_kernel_sample::protocol::codec_brotli}) {
    auto codec = request_for(kind, codec_wire);
    if (!invoke(device, codec, reply) ||
        reply.content_size != codec_text.size() || reply.field_count == 0 ||
        (reply.flags & ntl_net_kernel_sample::result_flag::codec_round_trip) ==
            0 ||
        std::memcmp(reply.transformed.data(), codec_text.data(),
                    codec_text.size()) != 0)
      return kind == ntl_net_kernel_sample::protocol::codec_gzip ? 15 : 16;
  }

  constexpr std::array<std::byte, 8> websocket_wire{
      std::byte{0x81},
      std::byte{0x82},
      std::byte{1},
      std::byte{2},
      std::byte{3},
      std::byte{4},
      byte(static_cast<char>('H' ^ 1)),
      byte(static_cast<char>('i' ^ 2))};
  auto websocket =
      request_for(ntl_net_kernel_sample::protocol::websocket, websocket_wire);
  if (!invoke(device, websocket, reply) || reply.content_size != 2 ||
      reply.transformed[0] != byte('H') || reply.transformed[1] != byte('i') ||
      (reply.flags & ntl_net_kernel_sample::result_flag::masked) == 0)
    return 4;

  constexpr std::array<std::byte, 15> qpack_wire{
      std::byte{0x00}, std::byte{0x00}, std::byte{0x51}, std::byte{0x0b},
      byte('/'),       byte('i'),       byte('n'),       byte('d'),
      byte('e'),       byte('x'),       byte('.'),       byte('h'),
      byte('t'),       byte('m'),       byte('l')};
  auto qpack = request_for(ntl_net_kernel_sample::protocol::qpack, qpack_wire);
  if (!invoke(device, qpack, reply) || reply.field_count != 1 ||
      reply.content_size != 16)
    return 5;

  constexpr std::array<std::byte, 1> advanced_wire{std::byte{1}};
  auto dynamic_qpack = request_for(
      ntl_net_kernel_sample::protocol::qpack_dynamic, advanced_wire);
  const bool dynamic_qpack_invoked = invoke(device, dynamic_qpack, reply);
  if (!dynamic_qpack_invoked || reply.field_count != 1 ||
      reply.content_size != 2 ||
      (reply.flags & ntl_net_kernel_sample::result_flag::qpack_resumed) == 0) {
    std::fprintf(stderr,
                 "dynamic QPACK contract failed: invoked=%d status=0x%08lx "
                 "size=%lu fields=%lu flags=0x%08lx\n",
                 dynamic_qpack_invoked ? 1 : 0,
                 static_cast<unsigned long>(reply.parse_status),
                 static_cast<unsigned long>(reply.content_size),
                 static_cast<unsigned long>(reply.field_count),
                 static_cast<unsigned long>(reply.flags));
    return 21;
  }

  auto webtransport_backend = request_for(
      ntl_net_kernel_sample::protocol::webtransport_backend, advanced_wire);
  if (!invoke(device, webtransport_backend, reply) || reply.field_count < 4 ||
      reply.content_size == 0 ||
      (reply.flags &
       ntl_net_kernel_sample::result_flag::webtransport_session) == 0)
    return 22;

  auto http_transform = request_for(
      ntl_net_kernel_sample::protocol::http_transform, advanced_wire);
  if (!invoke(device, http_transform, reply) || reply.field_count != 3 ||
      reply.content_size == 0 ||
      (reply.flags & ntl_net_kernel_sample::result_flag::http_all_versions) ==
          0 ||
      (reply.flags &
       ntl_net_kernel_sample::result_flag::http2_resume_stack_safe) == 0)
    return 23;

  auto msquic = request_for(
      ntl_net_kernel_sample::protocol::msquic_nmr, advanced_wire);
  if (!invoke(device, msquic, reply) ||
      reply.content_size !=
          ntl_net_kernel_sample::expected_msquic_api_version ||
      (reply.flags & ntl_net_kernel_sample::result_flag::msquic_nmr_bound) ==
          0)
    return 29;

  if (!run_async_stream_contract(device))
    return 24;

  for (const auto [kind, expected, failure] : {
           std::tuple{
               ntl_net_kernel_sample::protocol::workspace_lifetime,
               ntl_net_kernel_sample::result_flag::workspace_fail_closed |
                   ntl_net_kernel_sample::result_flag::
                       workspace_passive_cleanup,
               25},
           std::tuple{
               ntl_net_kernel_sample::protocol::wfp_injection_lifetime,
               ntl_net_kernel_sample::result_flag::
                   injection_passive_cleanup,
               26},
           std::tuple{
               ntl_net_kernel_sample::protocol::waitable_task_lifetime,
               ntl_net_kernel_sample::result_flag::task_passive_cleanup,
               27},
           std::tuple{
               ntl_net_kernel_sample::protocol::udp_mapping_lifetime,
               ntl_net_kernel_sample::result_flag::
                   udp_mapping_fail_closed,
               28},
           std::tuple{
               ntl_net_kernel_sample::protocol::bounded_wait_set,
               ntl_net_kernel_sample::result_flag::bounded_wait_blocks,
               30},
           std::tuple{
               ntl_net_kernel_sample::protocol::executor_lifetime,
               ntl_net_kernel_sample::result_flag::executor_lifetime_safe,
               31},
           std::tuple{
               ntl_net_kernel_sample::protocol::http3_origin_pool_lifetime,
               ntl_net_kernel_sample::result_flag::
                   http3_origin_pool_lifetime_safe,
               32}}) {
    auto lifetime = request_for(kind, advanced_wire);
    SetLastError(ERROR_SUCCESS);
    reply = {};
    const bool lifetime_invoked = invoke(device, lifetime, reply);
    const DWORD lifetime_error = GetLastError();
    if (!lifetime_invoked || (reply.flags & expected) != expected) {
      std::fprintf(
          stderr,
          "kernel lifetime contract failed: kind=%lu invoked=%d "
          "win32=%lu status=0x%08lx fields=%lu flags=0x%08lx "
          "expected=0x%08lx\n",
          static_cast<unsigned long>(kind), lifetime_invoked ? 1 : 0,
          static_cast<unsigned long>(lifetime_error),
          static_cast<unsigned long>(reply.parse_status),
          static_cast<unsigned long>(reply.field_count),
          static_cast<unsigned long>(reply.flags),
          static_cast<unsigned long>(expected));
      return failure;
    }
  }

  const auto client_hello_wire = make_client_hello();
  auto client_hello = request_for(
      ntl_net_kernel_sample::protocol::tls_client_hello, client_hello_wire);
  if (!invoke(device, client_hello, reply) || reply.field_count != 2 ||
      (reply.flags & ntl_net_kernel_sample::result_flag::server_name) == 0)
    return 6;

  constexpr std::string_view text = "kernel and user use one pipeline";
  auto transform =
      request_for(ntl_net_kernel_sample::protocol::transform,
                  std::as_bytes(std::span(text.data(), text.size())));
  if (!invoke(device, transform, reply) ||
      (reply.flags & ntl_net_kernel_sample::result_flag::transformed) == 0) {
    std::fwprintf(stderr,
                  L"transform failed: status=0x%08lx flags=0x%08lx "
                  L"size=%lu win32=%lu\n",
                  static_cast<unsigned long>(reply.parse_status),
                  static_cast<unsigned long>(reply.flags),
                  static_cast<unsigned long>(reply.content_size),
                  GetLastError());
    return 7;
  }
  const std::string_view transformed(
      reinterpret_cast<const char *>(reply.transformed.data()),
      reply.content_size);
  if (transformed != "KERNEL AND USER USE ONE PIPELINE")
    return 8;

  constexpr std::array<std::byte, 1> executor_wire{std::byte{1}};
  auto executor =
      request_for(ntl_net_kernel_sample::protocol::executor, executor_wire);
  if (!invoke(device, executor, reply) || reply.field_count != 1)
    return 9;

  constexpr std::array<std::byte, 1> crypto_wire{std::byte{1}};
  auto x509 =
      request_for(ntl_net_kernel_sample::protocol::x509_issue, crypto_wire);
  if (!invoke(device, x509, reply) || reply.content_size == 0 ||
      reply.field_count == 0 ||
      (reply.flags & ntl_net_kernel_sample::result_flag::x509_generated) == 0)
    return 18;
  PCCERT_CONTEXT leaf = CertCreateCertificateContext(
      X509_ASN_ENCODING | PKCS_7_ASN_ENCODING,
      reinterpret_cast<const BYTE *>(reply.transformed.data()),
      reply.content_size);
  if (!leaf)
    return 18;
  const bool leaf_is_v3 =
      leaf->pCertInfo && leaf->pCertInfo->dwVersion == CERT_V3;
  CertFreeCertificateContext(leaf);
  if (!leaf_is_v3)
    return 18;

  auto schannel = request_for(ntl_net_kernel_sample::protocol::schannel_client,
                              crypto_wire);
  if (!invoke(device, schannel, reply) || reply.content_size == 0 ||
      (reply.flags &
       ntl_net_kernel_sample::result_flag::schannel_client_hello) == 0 ||
      (reply.flags &
       ntl_net_kernel_sample::result_flag::credential_passive_cleanup) == 0)
    return 19;

  WSADATA winsock{};
  if (WSAStartup(MAKEWORD(2, 2), &winsock) != 0)
    return 17;

  constexpr std::string_view tls_text =
      "kernel WSK and Schannel coroutine round trip";
  SOCKET tls_listener = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
  if (tls_listener == INVALID_SOCKET) {
    WSACleanup();
    return 20;
  }
  sockaddr_in tls_address{};
  tls_address.sin_family = AF_INET;
  tls_address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  tls_address.sin_port = 0;
  if (bind(tls_listener, reinterpret_cast<const sockaddr *>(&tls_address),
           sizeof(tls_address)) == SOCKET_ERROR ||
      listen(tls_listener, 1) == SOCKET_ERROR) {
    closesocket(tls_listener);
    WSACleanup();
    return 20;
  }
  int tls_address_size = sizeof(tls_address);
  if (getsockname(tls_listener, reinterpret_cast<sockaddr *>(&tls_address),
                  &tls_address_size) == SOCKET_ERROR) {
    closesocket(tls_listener);
    WSACleanup();
    return 20;
  }

  std::atomic<bool> tls_server_ok{false};
  std::atomic<unsigned long> tls_server_error{0};
  try {
    // VMware Tools and service-style logons do not guarantee a loaded user
    // CAPI profile. This contract runs elevated and uses a uniquely named,
    // destructor-deleted machine key container instead.
    crtsys::wfp_sample::ephemeral_certificate authority(true);
    ntl::net::windows_tls_certificate_issuer issuer(
        authority.get(), {.key_name_prefix = L"ntl-kernel-contract-leaf",
                          .rsa_bits = 2048,
                          .validity_days = 1,
                          .machine_keys = true,
                          .reuse_leaf_key = false});
    ntl::net::tls_server_identity identity(issuer.issue(L"kernel.example"));
    std::thread tls_server([&]() {
      fd_set readable{};
      FD_ZERO(&readable);
      FD_SET(tls_listener, &readable);
      timeval timeout{20, 0};
      if (select(0, &readable, nullptr, nullptr, &timeout) != 1) {
        tls_server_error.store(1, std::memory_order_release);
        return;
      }
      SOCKET peer = accept(tls_listener, nullptr, nullptr);
      if (peer == INVALID_SOCKET) {
        tls_server_error.store(WSAGetLastError(),
                               std::memory_order_release);
        return;
      }
      try {
        ntl::net::io_completion_context context;
        ntl::net::async_socket socket(context, peer);
        peer = INVALID_SOCKET;
        ntl::net::tls_stream stream(socket, identity.credentials());
        auto operation = run_kernel_tls_server(stream, tls_text);
        const bool completed =
            ntl::net::user::sync_wait(std::move(operation));
        tls_server_ok.store(completed, std::memory_order_release);
        if (!completed)
          tls_server_error.store(2, std::memory_order_release);
        context.wait_for_idle();
      } catch (const std::exception &error) {
        std::fprintf(stderr, "kernel TLS server exception: %s\n",
                     error.what());
        tls_server_error.store(3, std::memory_order_release);
      } catch (...) {
        std::fprintf(stderr, "kernel TLS server unknown exception\n");
        tls_server_error.store(4, std::memory_order_release);
        if (peer != INVALID_SOCKET)
          closesocket(peer);
      }
    });

    const std::uint16_t tls_port = ntohs(tls_address.sin_port);
    std::vector<std::byte> tls_wire(sizeof(tls_port) + tls_text.size());
    std::memcpy(tls_wire.data(), &tls_port, sizeof(tls_port));
    std::memcpy(tls_wire.data() + sizeof(tls_port), tls_text.data(),
                tls_text.size());
    auto tls = request_for(ntl_net_kernel_sample::protocol::wsk_tls, tls_wire);
    const bool tls_invoked = invoke(device, tls, reply);
    tls_server.join();
    closesocket(tls_listener);
    if (!tls_invoked || !tls_server_ok.load(std::memory_order_acquire) ||
        reply.content_size != tls_text.size() ||
        reply.field_count != tls_text.size() ||
        (reply.flags & ntl_net_kernel_sample::result_flag::tls_round_trip) ==
            0 ||
        std::memcmp(reply.transformed.data(), tls_text.data(),
                    tls_text.size()) != 0) {
      std::fwprintf(
          stderr,
          L"kernel TLS contract failed: invoked=%d server=%d "
          L"status=0x%08lx size=%lu fields=%lu flags=0x%08lx "
          L"server_error=%lu win32=%lu\n",
          tls_invoked ? 1 : 0,
          tls_server_ok.load(std::memory_order_acquire) ? 1 : 0,
          static_cast<unsigned long>(reply.parse_status),
          static_cast<unsigned long>(reply.content_size),
          static_cast<unsigned long>(reply.field_count),
          static_cast<unsigned long>(reply.flags),
          tls_server_error.load(std::memory_order_acquire), GetLastError());
      WSACleanup();
      return 20;
    }
  } catch (const std::exception &error) {
    std::fprintf(stderr, "kernel TLS setup exception: %s\n", error.what());
    closesocket(tls_listener);
    WSACleanup();
    return 20;
  } catch (...) {
    std::fprintf(stderr, "kernel TLS setup unknown exception\n");
    closesocket(tls_listener);
    WSACleanup();
    return 20;
  }

  SOCKET listener = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
  if (listener == INVALID_SOCKET) {
    std::fprintf(stderr, "WSK echo listener socket failed: %lu\n",
                 static_cast<unsigned long>(WSAGetLastError()));
    WSACleanup();
    return 17;
  }
  sockaddr_in listen_address{};
  listen_address.sin_family = AF_INET;
  listen_address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  listen_address.sin_port = 0;
  if (bind(listener, reinterpret_cast<const sockaddr *>(&listen_address),
           sizeof(listen_address)) == SOCKET_ERROR ||
      listen(listener, 1) == SOCKET_ERROR) {
    std::fprintf(stderr, "WSK echo listener bind/listen failed: %lu\n",
                 static_cast<unsigned long>(WSAGetLastError()));
    closesocket(listener);
    WSACleanup();
    return 17;
  }
  int listen_address_size = sizeof(listen_address);
  if (getsockname(listener, reinterpret_cast<sockaddr *>(&listen_address),
                  &listen_address_size) == SOCKET_ERROR) {
    std::fprintf(stderr, "WSK echo listener getsockname failed: %lu\n",
                 static_cast<unsigned long>(WSAGetLastError()));
    closesocket(listener);
    WSACleanup();
    return 17;
  }

  constexpr std::string_view wsk_text =
      "bounded WSK TCP round trip through ntl::net";
  std::atomic<bool> echo_ok{false};
  std::atomic<int> echo_error{0};
  std::thread echo([&]() {
    fd_set readable{};
    FD_ZERO(&readable);
    FD_SET(listener, &readable);
    timeval timeout{15, 0};
    const int selected = select(0, &readable, nullptr, nullptr, &timeout);
    if (selected != 1) {
      echo_error.store(selected == SOCKET_ERROR ? WSAGetLastError()
                                                : WSAETIMEDOUT,
                       std::memory_order_release);
      return;
    }
    SOCKET peer = accept(listener, nullptr, nullptr);
    if (peer == INVALID_SOCKET) {
      echo_error.store(WSAGetLastError(), std::memory_order_release);
      return;
    }

    std::vector<char> bytes(wsk_text.size());
    std::size_t received = 0;
    while (received != bytes.size()) {
      const int count = recv(peer, bytes.data() + received,
                             static_cast<int>(bytes.size() - received), 0);
      if (count <= 0) {
        echo_error.store(count == SOCKET_ERROR ? WSAGetLastError()
                                               : WSAECONNRESET,
                         std::memory_order_release);
        closesocket(peer);
        return;
      }
      received += static_cast<std::size_t>(count);
    }
    std::size_t sent = 0;
    while (sent != bytes.size()) {
      const int count = send(peer, bytes.data() + sent,
                             static_cast<int>(bytes.size() - sent), 0);
      if (count <= 0) {
        echo_error.store(count == SOCKET_ERROR ? WSAGetLastError()
                                               : WSAECONNRESET,
                         std::memory_order_release);
        closesocket(peer);
        return;
      }
      sent += static_cast<std::size_t>(count);
    }
    (void)shutdown(peer, SD_SEND);
    closesocket(peer);
    echo_ok.store(true, std::memory_order_release);
  });

  const std::uint16_t wsk_port = ntohs(listen_address.sin_port);
  std::vector<std::byte> wsk_wire(sizeof(wsk_port) + wsk_text.size());
  std::memcpy(wsk_wire.data(), &wsk_port, sizeof(wsk_port));
  std::memcpy(wsk_wire.data() + sizeof(wsk_port), wsk_text.data(),
              wsk_text.size());
  auto wsk = request_for(ntl_net_kernel_sample::protocol::wsk_tcp, wsk_wire);
  SetLastError(ERROR_SUCCESS);
  reply = {};
  const bool wsk_invoked = invoke(device, wsk, reply);
  const DWORD wsk_error = GetLastError();
  echo.join();
  closesocket(listener);
  if (!wsk_invoked || !echo_ok.load(std::memory_order_acquire) ||
      reply.content_size != wsk_text.size() ||
      reply.field_count != wsk_text.size() ||
      (reply.flags & ntl_net_kernel_sample::result_flag::wsk_round_trip) == 0 ||
      std::memcmp(reply.transformed.data(), wsk_text.data(), wsk_text.size()) !=
          0) {
    std::fprintf(
        stderr,
        "WSK connect contract failed: invoked=%d echo=%d win32=%lu "
        "echo_error=%d status=0x%08lx size=%lu fields=%lu flags=0x%08lx\n",
        wsk_invoked ? 1 : 0,
        echo_ok.load(std::memory_order_acquire) ? 1 : 0,
        static_cast<unsigned long>(wsk_error),
        echo_error.load(std::memory_order_acquire),
        static_cast<unsigned long>(reply.parse_status),
        static_cast<unsigned long>(reply.content_size),
        static_cast<unsigned long>(reply.field_count),
        static_cast<unsigned long>(reply.flags));
    return 17;
  }

  SOCKET reservation = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
  if (reservation == INVALID_SOCKET) {
    std::fprintf(stderr, "WSK listener port socket failed: %lu\n",
                 static_cast<unsigned long>(WSAGetLastError()));
    WSACleanup();
    return 21;
  }
  sockaddr_in reservation_address{};
  reservation_address.sin_family = AF_INET;
  reservation_address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  reservation_address.sin_port = 0;
  if (bind(reservation,
           reinterpret_cast<const sockaddr *>(&reservation_address),
           sizeof(reservation_address)) == SOCKET_ERROR) {
    std::fprintf(stderr, "WSK listener port bind failed: %lu\n",
                 static_cast<unsigned long>(WSAGetLastError()));
    closesocket(reservation);
    WSACleanup();
    return 21;
  }
  int reservation_size = sizeof(reservation_address);
  if (getsockname(reservation,
                  reinterpret_cast<sockaddr *>(&reservation_address),
                  &reservation_size) == SOCKET_ERROR) {
    std::fprintf(stderr, "WSK listener getsockname failed: %lu\n",
                 static_cast<unsigned long>(WSAGetLastError()));
    closesocket(reservation);
    WSACleanup();
    return 21;
  }
  const std::uint16_t kernel_listener_port =
      ntohs(reservation_address.sin_port);
  closesocket(reservation);

  constexpr std::string_view listener_text =
      "accepted WSK transport through ntl::net";
  const auto listener_test_started = std::chrono::steady_clock::now();
  std::atomic<bool> listener_client_ok{false};
  std::atomic<int> listener_client_error{0};
  std::atomic<std::size_t> listener_client_attempts{0};
  std::atomic<long long> listener_client_started_ms{-1};
  std::atomic<long long> listener_client_connected_ms{-1};
  std::atomic<unsigned int> listener_client_local_port{0};
  std::thread listener_client([&]() {
    listener_client_started_ms.store(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - listener_test_started)
            .count(),
        std::memory_order_release);
    SOCKET peer = INVALID_SOCKET;
    // Cover the complete bounded provider-capture and accept interval. Under
    // Driver Verifier, a five-second producer window can expire before the
    // listening socket is ready and only measures verifier scheduling delay.
    for (std::size_t attempt = 0; attempt != 1'500; ++attempt) {
      listener_client_attempts.store(attempt + 1, std::memory_order_relaxed);
      peer = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
      if (peer == INVALID_SOCKET) {
        listener_client_error.store(WSAGetLastError(),
                                    std::memory_order_relaxed);
        return;
      }
      sockaddr_in address{};
      address.sin_family = AF_INET;
      address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
      address.sin_port = htons(kernel_listener_port);
      if (connect(peer, reinterpret_cast<const sockaddr *>(&address),
                  sizeof(address)) == 0) {
        sockaddr_in connected_local{};
        int connected_local_size = sizeof(connected_local);
        if (getsockname(peer,
                        reinterpret_cast<sockaddr *>(&connected_local),
                        &connected_local_size) == 0) {
          listener_client_local_port.store(
              ntohs(connected_local.sin_port), std::memory_order_release);
        }
        listener_client_connected_ms.store(
            std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - listener_test_started)
                .count(),
            std::memory_order_release);
        listener_client_error.store(0, std::memory_order_relaxed);
        break;
      }
      listener_client_error.store(WSAGetLastError(),
                                  std::memory_order_relaxed);
      closesocket(peer);
      peer = INVALID_SOCKET;
      std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    if (peer == INVALID_SOCKET)
      return;
    std::size_t sent = 0;
    while (sent != listener_text.size()) {
      const int amount = send(peer, listener_text.data() + sent,
                              static_cast<int>(listener_text.size() - sent), 0);
      if (amount <= 0) {
        listener_client_error.store(WSAGetLastError(),
                                    std::memory_order_relaxed);
        closesocket(peer);
        return;
      }
      sent += static_cast<std::size_t>(amount);
    }
    std::vector<char> echoed(listener_text.size());
    std::size_t received = 0;
    while (received != echoed.size()) {
      const int amount = recv(peer, echoed.data() + received,
                              static_cast<int>(echoed.size() - received), 0);
      if (amount <= 0) {
        listener_client_error.store(WSAGetLastError(),
                                    std::memory_order_relaxed);
        closesocket(peer);
        return;
      }
      received += static_cast<std::size_t>(amount);
    }
    closesocket(peer);
    listener_client_ok.store(
        std::equal(echoed.begin(), echoed.end(), listener_text.begin()),
        std::memory_order_release);
  });
  std::vector<std::byte> listener_wire(sizeof(kernel_listener_port) +
                                       listener_text.size());
  std::memcpy(listener_wire.data(), &kernel_listener_port,
              sizeof(kernel_listener_port));
  std::memcpy(listener_wire.data() + sizeof(kernel_listener_port),
              listener_text.data(), listener_text.size());
  auto listener_request =
      request_for(ntl_net_kernel_sample::protocol::wsk_listener, listener_wire);
  const bool listener_invoked = invoke(device, listener_request, reply);
  listener_client.join();
  WSACleanup();
  if (!listener_invoked ||
      !listener_client_ok.load(std::memory_order_acquire) ||
      reply.content_size != listener_text.size() ||
      reply.field_count != listener_text.size() ||
      (reply.flags &
       ntl_net_kernel_sample::result_flag::wsk_listener_round_trip) == 0 ||
      std::memcmp(reply.transformed.data(), listener_text.data(),
                  listener_text.size()) != 0) {
    std::fprintf(
        stderr,
        "WSK listener contract failed: invoked=%d client=%d "
        "status=0x%08lx size=%lu fields=%lu flags=0x%08lx "
        "client_error=%d attempts=%zu client_started_ms=%lld "
        "client_connected_ms=%lld server_port=%u client_local_port=%u "
        "prior_listener_port=%u\n",
        listener_invoked ? 1 : 0,
        listener_client_ok.load(std::memory_order_acquire) ? 1 : 0,
        static_cast<unsigned long>(reply.parse_status),
        static_cast<unsigned long>(reply.content_size),
        static_cast<unsigned long>(reply.field_count),
        static_cast<unsigned long>(reply.flags),
        listener_client_error.load(std::memory_order_relaxed),
        listener_client_attempts.load(std::memory_order_relaxed),
        listener_client_started_ms.load(std::memory_order_acquire),
        listener_client_connected_ms.load(std::memory_order_acquire),
        static_cast<unsigned int>(kernel_listener_port),
        listener_client_local_port.load(std::memory_order_acquire),
        static_cast<unsigned int>(wsk_port));
    return 21;
  }

  std::wprintf(
      L"kernel network core ok: HTTP/1=%zu HTTP/2=3 HTTP/3=3 gRPC=%zu "
      L"WebSocket=2 WebTransport=2+session QPACK=static+dynamic TLS=2 "
      L"HTTP-transform=1/2/3 "
      L"transformed=%zu "
      L"gzip=1 Brotli=1 WSK=connect+listen TLS-roundtrip=1 X509=1 Schannel=1 "
      L"MsQuic=NMR+passive-cleanup "
      L"async-stream=serialized-write+read-races workspace=fail-closed+"
      L"passive-cleanup injection=passive-cleanup offload=1 executor=1\n",
      http1_wire.size(), grpc_payload.size(), transformed.size());
  return 0;
}
