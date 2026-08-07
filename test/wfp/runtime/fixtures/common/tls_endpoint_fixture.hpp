#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>

#include <array>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <future>
#include <memory>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include <ntl/net/http/transform>
#include <ntl/net/http2/transform>
#include <ntl/net/inspection/content_decoder>
#include <ntl/net/inspection/content_encoder>
#include <ntl/net/io/async_socket>
#include <ntl/net/tls/framed_stream>
#include <ntl/net/tls/stream>
#include <ntl/net/user/task>

#include "http1_support.hpp"
#include "windows_support.hpp"

namespace crtsys::test::wfp::tls_endpoint_fixture {

using crtsys::wfp_sample::listener;
using crtsys::wfp_sample::socket_owner;

struct endpoint_options {
  std::wstring server_name;
  std::string authority;
  std::string transformed_header;
  std::string response_body;
};

struct client_result {
  bool received = false;
  unsigned status = 0;
  std::string body;
};

struct origin_result {
  bool received = false;
  bool transformed_header = false;
};

inline socket_owner connect_loopback(int family, std::uint16_t port) {
  socket_owner socket(::WSASocketW(family, SOCK_STREAM, IPPROTO_TCP, nullptr,
                                   0, WSA_FLAG_OVERLAPPED));
  if (socket.get() == INVALID_SOCKET)
    crtsys::wfp_sample::throw_socket("WSASocketW(TLS fixture client)");
  sockaddr_storage storage{};
  int size = 0;
  if (family == AF_INET) {
    auto &address = reinterpret_cast<sockaddr_in &>(storage);
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    address.sin_port = htons(port);
    size = sizeof(address);
  } else if (family == AF_INET6) {
    auto &address = reinterpret_cast<sockaddr_in6 &>(storage);
    address.sin6_family = AF_INET6;
    address.sin6_addr = in6addr_loopback;
    address.sin6_port = htons(port);
    size = sizeof(address);
  } else {
    throw std::invalid_argument("TLS fixture family is invalid");
  }
  if (::connect(socket.get(), reinterpret_cast<const sockaddr *>(&storage),
                size) == SOCKET_ERROR)
    crtsys::wfp_sample::throw_socket("connect(TLS fixture)");
  return socket;
}

inline std::vector<std::byte> make_http1_request(
    const endpoint_options &options, bool block) {
  const std::string body = block ? "BLOCKME:test" : "ALLOW:test";
  std::string header =
      "POST /inspect HTTP/1.1\r\nHost: " + options.authority +
      "\r\nContent-Type: text/plain\r\n";
  if (block)
    header += "X-NTL-Block: 1\r\n";
  header += "Content-Length: " + std::to_string(body.size()) +
            "\r\nConnection: close\r\n\r\n";
  std::vector<std::byte> result(header.size() + body.size());
  std::memcpy(result.data(), header.data(), header.size());
  std::memcpy(result.data() + header.size(), body.data(), body.size());
  return result;
}

inline ntl::net::user::task<unsigned>
close_tls(ntl::net::tls_stream &stream) {
  co_await stream.shutdown();
  std::array<std::byte, 4096> discard{};
  while (!stream.received_close_notify()) {
    if (co_await stream.read_some_borrowed(discard) == 0)
      break;
  }
  if (!stream.received_close_notify())
    throw std::runtime_error("TLS fixture peer omitted close_notify");
  co_return 0;
}

inline ntl::net::user::task<client_result> run_http1_client(
    ntl::net::tls_stream &stream,
    std::shared_ptr<ntl::net::tls_peer_certificate_policy> certificate,
    const endpoint_options &options, bool block) {
  co_await stream.handshake_client(
      {.server_name = options.server_name,
       .certificate_policy = std::move(certificate),
       .application_protocols = {"http/1.1"},
       .require_application_protocol = true});
  const auto request = make_http1_request(options, block);
  if (co_await stream.write_all(request) != request.size())
    throw std::runtime_error("HTTP/1 fixture request write was short");
  ntl::net::tls_framed_stream replies(
      stream,
      crtsys::wfp_sample::make_http_framer(
          ntl::net::http::http1_message_kind::response),
      {crtsys::wfp_sample::maximum_http_message_size}, 231);
  auto reply = co_await replies.read_frame_or_eof();
  if (!reply) {
    co_await close_tls(stream);
    co_return client_result{};
  }
  auto parsed = crtsys::wfp_sample::parse_http_response(*reply);
  client_result result{
      true, parsed.status,
      std::string(reinterpret_cast<const char *>(parsed.body.data()),
                  parsed.body.size())};
  co_await close_tls(stream);
  co_return result;
}

inline client_result exchange_http1(
    int family, std::uint16_t port,
    ntl::net::tls_credentials &credentials,
    std::shared_ptr<ntl::net::tls_peer_certificate_policy> certificate,
    const endpoint_options &options, bool block) {
  auto native = connect_loopback(family, port);
  ntl::net::io_completion_context context;
  ntl::net::async_socket socket(context, native.release());
  ntl::net::tls_stream stream(socket, credentials);
  auto operation =
      run_http1_client(stream, certificate, options, block);
  auto result = ntl::net::user::sync_wait(std::move(operation));
  context.wait_for_idle();
  return result;
}

inline ntl::net::user::task<unsigned> read_exactly(
    ntl::net::tls_stream &stream, std::span<std::byte> output) {
  std::size_t used = 0;
  while (used != output.size()) {
    const auto received = co_await stream.read_some_borrowed(output.subspan(used));
    if (received == 0)
      throw std::runtime_error(
          "HTTP/2 fixture stream ended early after " +
          std::to_string(used) + " of " +
          std::to_string(output.size()) + " bytes");
    used += received;
  }
  co_return 0;
}

inline constexpr std::string_view h2_preface =
    "PRI * HTTP/2.0\r\n\r\nSM\r\n\r\n";
inline constexpr std::array<std::byte, 9> h2_settings{
    std::byte{}, std::byte{}, std::byte{}, std::byte{0x04}, std::byte{},
    std::byte{}, std::byte{}, std::byte{}, std::byte{}};

struct wire_frame {
  std::array<std::byte, ntl::net::http2::frame_header_size +
                            ntl::net::http2::default_maximum_frame_size>
      bytes{};
  std::size_t size = 0;
};

inline ntl::net::user::task<wire_frame> read_frame(
    ntl::net::tls_stream &stream) {
  wire_frame wire;
  co_await read_exactly(
      stream, std::span(wire.bytes).first(ntl::net::http2::frame_header_size));
  const auto header = ntl::net::http2::inspect_header(
      ntl::net::scatter_view::from_contiguous(
          std::span(wire.bytes).first(ntl::net::http2::frame_header_size)));
  if (!header ||
      header->payload_size > ntl::net::http2::default_maximum_frame_size)
    throw std::runtime_error("HTTP/2 fixture frame is invalid");
  if (header->payload_size != 0)
    co_await read_exactly(
        stream, std::span(wire.bytes).subspan(
                    ntl::net::http2::frame_header_size,
                    header->payload_size));
  wire.size = ntl::net::http2::frame_header_size + header->payload_size;
  co_return wire;
}

inline ntl::net::user::task<std::optional<wire_frame>> read_frame_or_eof(
    ntl::net::tls_stream &stream) {
  wire_frame wire;
  auto header = std::span(wire.bytes).first(
      ntl::net::http2::frame_header_size);
  const auto first = co_await stream.read_some_borrowed(header);
  if (first == 0)
    co_return std::nullopt;
  if (first < header.size())
    co_await read_exactly(stream, header.subspan(first));
  const auto parsed = ntl::net::http2::inspect_header(
      ntl::net::scatter_view::from_contiguous(header));
  if (!parsed ||
      parsed->payload_size > ntl::net::http2::default_maximum_frame_size)
    throw std::runtime_error("HTTP/2 fixture frame is invalid");
  if (parsed->payload_size != 0)
    co_await read_exactly(
        stream, std::span(wire.bytes).subspan(
                    ntl::net::http2::frame_header_size,
                    parsed->payload_size));
  wire.size = ntl::net::http2::frame_header_size + parsed->payload_size;
  co_return wire;
}

inline ntl::net::user::task<unsigned> write_frames(
    ntl::net::tls_stream &stream,
    std::span<const ntl::net::http2::outbound_frame> frames) {
  for (const auto &frame : frames) {
    if (co_await stream.write_all(frame.wire) != frame.wire.size())
      throw std::runtime_error("HTTP/2 fixture frame write was short");
  }
  co_return 0;
}

inline ntl::net::user::task<client_result> run_http2_client(
    ntl::net::tls_stream &stream,
    std::shared_ptr<ntl::net::tls_peer_certificate_policy> certificate,
    const endpoint_options &options, bool block) {
  co_await stream.handshake_client(
      {.server_name = options.server_name,
       .certificate_policy = std::move(certificate),
       .application_protocols = {"h2"},
       .require_application_protocol = true});
  const auto preface = std::as_bytes(std::span(h2_preface));
  if (co_await stream.write_all(preface) != preface.size() ||
      co_await stream.write_all(h2_settings) != h2_settings.size())
    throw std::runtime_error("HTTP/2 fixture preface write was short");
  ntl::net::http::request_message request;
  request.wire_protocol = ntl::net::http::protocol::http2;
  request.method = "GET";
  request.scheme = "https";
  request.authority = options.authority;
  request.path = "/inspect";
  if (block)
    request.headers.set("x-ntl-block", "1");
  auto frames = ntl::net::http2::encode_request_frames(1, request, {});
  if (!frames)
    throw std::runtime_error("HTTP/2 fixture request encoding failed");
  co_await write_frames(stream, *frames);

  ntl::net::http::transform_pipeline pipeline;
  ntl::net::inspection::content_decoder_registry decoders;
  ntl::net::inspection::content_encoder_registry encoders;
  auto exchanges = std::make_shared<ntl::net::http2::exchange_store>();
  if (!exchanges->remember(1, request).is_ok())
    throw std::runtime_error("HTTP/2 fixture exchange state failed");
  ntl::net::http2::connection_transformer responses(
      ntl::net::http2::connection_direction::responses, exchanges, pipeline,
      decoders, encoders);
  std::optional<ntl::net::http::response_message> response;
  std::string frame_trace;
  for (unsigned count = 0; count != 64 && !response; ++count) {
    wire_frame wire;
    try {
      wire = co_await read_frame(stream);
    } catch (const std::exception &error) {
      throw std::runtime_error(
          std::string(error.what()) + "; received frames: " + frame_trace);
    }
    auto frame = ntl::net::http2::frame_view::parse(
        ntl::net::scatter_view::from_contiguous(
            std::span(wire.bytes).first(wire.size)));
    if (!frame)
      throw std::runtime_error("HTTP/2 fixture response frame is invalid");
    frame_trace += "type=" +
                   std::to_string(
                       static_cast<unsigned>(frame->header().type)) +
                   ",flags=" +
                   std::to_string(frame->header().flags) +
                   ",payload=" +
                   std::to_string(frame->header().payload_size) + ";";
    if (frame->header().type == ntl::net::http2::frame_type::settings) {
      if (!frame->header().acknowledgement()) {
        auto acknowledgement = h2_settings;
        acknowledgement[4] = std::byte{0x01};
        if (co_await stream.write_all(acknowledgement) !=
            acknowledgement.size())
          throw std::runtime_error("HTTP/2 SETTINGS ack was short");
      }
      continue;
    }
    auto transformed = responses.consume(*frame);
    if (!transformed)
      throw std::runtime_error("HTTP/2 fixture response transform failed");
    if (transformed->message_complete && transformed->response)
      response = std::move(transformed->response);
  }
  if (!response)
    throw std::runtime_error("HTTP/2 fixture response did not complete");
  client_result result{
      true, response->status,
      std::string(reinterpret_cast<const char *>(response->body.data()),
                  response->body.size())};
  co_await close_tls(stream);
  co_return result;
}

inline client_result exchange_http2(
    int family, std::uint16_t port,
    ntl::net::tls_credentials &credentials,
    std::shared_ptr<ntl::net::tls_peer_certificate_policy> certificate,
    const endpoint_options &options, bool block) {
  auto native = connect_loopback(family, port);
  ntl::net::io_completion_context context;
  ntl::net::async_socket socket(context, native.release());
  ntl::net::tls_stream stream(socket, credentials);
  auto operation =
      run_http2_client(stream, certificate, options, block);
  auto result = ntl::net::user::sync_wait(std::move(operation));
  context.wait_for_idle();
  return result;
}

inline ntl::net::user::task<origin_result> run_http1_origin(
    ntl::net::tls_stream &stream, const endpoint_options &options) {
  co_await stream.handshake_server(
      {.application_protocols = {"http/1.1"},
       .require_application_protocol = true});
  ntl::net::tls_framed_stream requests(
      stream,
      crtsys::wfp_sample::make_http_framer(
          ntl::net::http::http1_message_kind::request),
      {crtsys::wfp_sample::maximum_http_message_size}, 257);
  auto request = co_await requests.read_frame_or_eof();
  if (!request) {
    co_await close_tls(stream);
    co_return origin_result{};
  }
  const std::string_view wire(
      reinterpret_cast<const char *>(request->frame().data()),
      request->frame().size());
  origin_result result{
      true, wire.find(options.transformed_header) != std::string_view::npos};
  const std::string header =
      "HTTP/1.1 200 OK\r\nContent-Type: text/html; charset=utf-8\r\n"
      "Content-Length: " +
      std::to_string(options.response_body.size()) +
      "\r\nConnection: close\r\n\r\n";
  std::vector<std::byte> response(header.size() + options.response_body.size());
  std::memcpy(response.data(), header.data(), header.size());
  std::memcpy(response.data() + header.size(), options.response_body.data(),
              options.response_body.size());
  if (co_await stream.write_all(response) != response.size())
    throw std::runtime_error("HTTP/1 fixture origin response was short");
  co_await close_tls(stream);
  co_return result;
}

inline ntl::net::user::task<origin_result> run_http2_origin(
    ntl::net::tls_stream &stream, const endpoint_options &options) {
  co_await stream.handshake_server(
      {.application_protocols = {"h2"},
       .require_application_protocol = true});
  std::array<std::byte, h2_preface.size()> preface{};
  const auto first = co_await stream.read_some_borrowed(preface);
  if (first == 0) {
    co_await close_tls(stream);
    co_return origin_result{};
  }
  if (first < preface.size())
    co_await read_exactly(stream, std::span(preface).subspan(first));
  if (std::memcmp(preface.data(), h2_preface.data(), preface.size()) != 0)
    throw std::runtime_error("HTTP/2 fixture origin preface is invalid");
  if (co_await stream.write_all(h2_settings) != h2_settings.size())
    throw std::runtime_error("HTTP/2 fixture origin SETTINGS was short");
  ntl::net::http::transform_pipeline pipeline;
  ntl::net::inspection::content_decoder_registry decoders;
  ntl::net::inspection::content_encoder_registry encoders;
  auto exchanges = std::make_shared<ntl::net::http2::exchange_store>();
  ntl::net::http2::connection_transformer requests(
      ntl::net::http2::connection_direction::requests, exchanges, pipeline,
      decoders, encoders);
  std::optional<ntl::net::http::request_message> request;
  std::uint32_t stream_id = 0;
  for (unsigned count = 0; count != 64 && !request; ++count) {
    auto maybe_wire = co_await read_frame_or_eof(stream);
    if (!maybe_wire) {
      co_await close_tls(stream);
      co_return origin_result{};
    }
    auto wire = std::move(*maybe_wire);
    auto frame = ntl::net::http2::frame_view::parse(
        ntl::net::scatter_view::from_contiguous(
            std::span(wire.bytes).first(wire.size)));
    if (!frame)
      throw std::runtime_error("HTTP/2 fixture origin frame is invalid");
    if (frame->header().type == ntl::net::http2::frame_type::settings) {
      if (!frame->header().acknowledgement()) {
        auto acknowledgement = h2_settings;
        acknowledgement[4] = std::byte{0x01};
        if (co_await stream.write_all(acknowledgement) !=
            acknowledgement.size())
          throw std::runtime_error("HTTP/2 origin SETTINGS ack was short");
      }
      continue;
    }
    auto transformed = requests.consume(*frame);
    if (!transformed)
      throw std::runtime_error("HTTP/2 fixture origin parse failed");
    if (transformed->message_complete && transformed->request) {
      stream_id = transformed->stream_id;
      request = std::move(transformed->request);
    }
  }
  if (!request || stream_id == 0)
    throw std::runtime_error("HTTP/2 fixture origin request did not complete");
  const bool transformed =
      request->headers.first(options.transformed_header.substr(
          0, options.transformed_header.find(':'))) == "1";
  ntl::net::http::response_message response;
  response.wire_protocol = ntl::net::http::protocol::http2;
  response.status = 200;
  response.headers.append("content-type", "text/html; charset=utf-8");
  response.headers.append("content-length",
                          std::to_string(options.response_body.size()));
  auto frames = ntl::net::http2::encode_response_frames(
      stream_id, response, std::as_bytes(std::span(options.response_body)));
  if (!frames)
    throw std::runtime_error("HTTP/2 fixture origin response failed");
  co_await write_frames(stream, *frames);
  co_await close_tls(stream);
  co_return origin_result{true, transformed};
}

inline std::future<origin_result> start_origin(
    const listener &origin, ntl::net::tls_credentials &credentials,
    const endpoint_options &options, bool http2) {
  return std::async(std::launch::async,
                    [&origin, &credentials, &options, http2]() {
    fd_set readable;
    FD_ZERO(&readable);
    FD_SET(origin.socket.get(), &readable);
    timeval timeout{10, 0};
    if (::select(0, &readable, nullptr, nullptr, &timeout) != 1)
      throw std::runtime_error("TLS fixture origin accept timed out");
    auto native = crtsys::wfp_sample::accept_one(origin);
    const SOCKET accepted = native.get();
    std::atomic<bool> completed{false};
    std::jthread watchdog([&](std::stop_token stop) {
      for (unsigned count = 0; count != 100 && !stop.stop_requested();
           ++count) {
        if (completed.load(std::memory_order_acquire))
          return;
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
      }
      if (!completed.load(std::memory_order_acquire))
        (void)::shutdown(accepted, SD_BOTH);
    });
    ntl::net::io_completion_context context;
    ntl::net::async_socket socket(context, native.release());
    ntl::net::tls_stream stream(socket, credentials);
    try {
      auto operation = http2 ? run_http2_origin(stream, options)
                             : run_http1_origin(stream, options);
      auto result = ntl::net::user::sync_wait(std::move(operation));
      completed.store(true, std::memory_order_release);
      watchdog.request_stop();
      context.wait_for_idle();
      return result;
    } catch (...) {
      completed.store(true, std::memory_order_release);
      watchdog.request_stop();
      (void)::shutdown(accepted, SD_BOTH);
      context.wait_for_idle();
      throw;
    }
  });
}

inline bool has_pending_connection(const listener &origin) {
  return crtsys::wfp_sample::has_pending_connection(origin);
}

inline void prove_raw_direct(const listener &origin, int family) {
  std::thread server([&]() {
    auto peer = crtsys::wfp_sample::accept_one(origin);
    char value = 0;
    if (::recv(peer.get(), &value, 1, 0) == 1 && value == 'D')
      (void)::send(peer.get(), "R", 1, 0);
  });
  auto client = connect_loopback(family, origin.port);
  if (::send(client.get(), "D", 1, 0) != 1) {
    server.join();
    crtsys::wfp_sample::throw_socket("send(direct proof)");
  }
  char response = 0;
  const int received = ::recv(client.get(), &response, 1, 0);
  server.join();
  if (received != 1 || response != 'R')
    throw std::runtime_error("TLS redirect policy was not removed");
}

} // namespace crtsys::test::wfp::tls_endpoint_fixture
