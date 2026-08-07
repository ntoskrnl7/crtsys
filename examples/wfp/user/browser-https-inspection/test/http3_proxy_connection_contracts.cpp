#include <atomic>
#include <array>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include <ntl/net/http3/async_origin_pool>
#include <ntl/net/http3/proxy_connection>
#include <ntl/net/http/inspection_policy>

namespace {

class recording_backend
    : public ntl::net::quic::transport_backend {
public:
  ntl::net::quic::backend_capabilities
  capabilities() const noexcept override {
    return {.available = true,
            .tls13_termination = true,
            .qpack_dynamic_table = true,
            .bidirectional_streams = true,
            .unidirectional_streams = true};
  }

  ntl::status run_borrowed(
      ntl::net::quic::backend_sink &) noexcept override {
    return ntl::status::ok();
  }

  ntl::status write_stream(
      std::uint64_t, ntl::net::scatter_view plaintext,
      bool final) noexcept override {
    try {
      std::vector<std::byte> bytes(plaintext.size());
      if (!bytes.empty()) {
        const auto copied = plaintext.copy_to(bytes);
        if (!copied.is_ok())
          return copied;
      }
      writes.push_back(std::move(bytes));
      if (final)
        final_writes.fetch_add(1, std::memory_order_release);
      return ntl::status::ok();
    } catch (...) {
      return STATUS_INSUFFICIENT_RESOURCES;
    }
  }

  ntl::status write_qpack_decoder_stream(
      ntl::net::scatter_view) noexcept override {
    return ntl::status::ok();
  }

  ntl::status open_unidirectional_stream(
      std::uint64_t &stream_id) noexcept override {
    stream_id = next_unidirectional;
    next_unidirectional += 4;
    return ntl::status::ok();
  }

  ntl::status reset_stream(
      std::uint64_t, std::uint64_t) noexcept override {
    ++resets;
    return ntl::status::ok();
  }

  void stop() noexcept override {}
  ntl::status drain() noexcept override {
    return ntl::status::ok();
  }

  std::vector<std::vector<std::byte>> writes;
  std::atomic<std::size_t> final_writes{0};
  std::uint64_t next_unidirectional = 2;
  std::size_t resets = 0;
};

class blocking_run_backend final
    : public recording_backend {
public:
  ntl::status run_borrowed(
      ntl::net::quic::backend_sink &) noexcept override {
    std::unique_lock guard(lock_);
    entered_ = true;
    changed_.notify_all();
    changed_.wait(guard, [&] { return released_; });
    return ntl::status::ok();
  }

  bool wait_entered() noexcept {
    std::unique_lock guard(lock_);
    return changed_.wait_for(
        guard, std::chrono::seconds(5), [&] { return entered_; });
  }

  void release() noexcept {
    std::lock_guard guard(lock_);
    released_ = true;
    changed_.notify_all();
  }

private:
  std::mutex lock_;
  std::condition_variable changed_;
  bool entered_ = false;
  bool released_ = false;
};

class counting_origin final
    : public ntl::net::http3::origin_transport {
public:
  ntl::result<ntl::net::http3::origin_response>
  send(const ntl::net::http3::origin_request &) noexcept override {
    calls.fetch_add(1, std::memory_order_relaxed);
    ntl::net::http3::origin_response response;
    response.status = 204;
    response.negotiated_protocol = "h3";
    return ntl::ok(std::move(response));
  }

  std::atomic<std::size_t> calls{0};
};

class failing_origin final
    : public ntl::net::http3::origin_transport {
public:
  ntl::result<ntl::net::http3::origin_response>
  send(const ntl::net::http3::origin_request &) noexcept override {
    return ntl::unexpected(STATUS_CONNECTION_DISCONNECTED);
  }
};

class blocking_origin final
    : public ntl::net::http3::origin_transport {
public:
  ntl::result<ntl::net::http3::origin_response>
  send(const ntl::net::http3::origin_request &) noexcept override {
    std::unique_lock guard(lock);
    ++entered;
    changed.notify_all();
    changed.wait(guard, [&] { return released; });
    ntl::net::http3::origin_response response;
    response.status = 204;
    response.negotiated_protocol = "h3";
    return ntl::ok(std::move(response));
  }

  bool wait_entered(std::size_t count) {
    std::unique_lock guard(lock);
    return changed.wait_for(
        guard, std::chrono::seconds(5),
        [&] { return entered == count; });
  }

  void release() noexcept {
    std::lock_guard guard(lock);
    released = true;
    changed.notify_all();
  }

  std::mutex lock;
  std::condition_variable changed;
  std::size_t entered = 0;
  bool released = false;
};

class stage_observer final
    : public ntl::net::http3::proxy_connection_observer {
public:
  void on_inspection(
      const ntl::net::http::inspection_context_view &context) noexcept override {
    if (context.direction() ==
        ntl::net::http::message_direction::request) {
      if (context.stage() ==
          ntl::net::http::inspection_stage::headers)
        ++request_headers;
      else if (context.stage() ==
               ntl::net::http::inspection_stage::body_chunk)
        ++request_body_chunks;
      else
        ++request_complete;
    } else {
      if (context.stage() ==
          ntl::net::http::inspection_stage::headers)
        ++response_headers;
      else if (context.stage() ==
               ntl::net::http::inspection_stage::body_chunk)
        ++response_body_chunks;
      else
        ++response_complete;
    }
  }

  std::size_t request_headers = 0;
  std::size_t request_body_chunks = 0;
  std::size_t request_complete = 0;
  std::size_t response_headers = 0;
  std::size_t response_body_chunks = 0;
  std::size_t response_complete = 0;
};

class completion_observer final
    : public ntl::net::http3::proxy_connection_observer {
public:
  void on_exchange_complete(
      std::uint64_t,
      const ntl::net::http::request_message &,
      const ntl::net::http::response_message &response,
      bool terminal) noexcept override {
    status = response.status;
    terminal_response = terminal;
    ++completions;
  }

  unsigned status = 0;
  bool terminal_response = false;
  std::size_t completions = 0;
};

class waiting_terminal_observer final
    : public ntl::net::http3::proxy_connection_observer {
public:
  void on_exchange_complete(
      std::uint64_t,
      const ntl::net::http::request_message &,
      const ntl::net::http::response_message &response,
      bool terminal) noexcept override {
    {
      std::lock_guard guard(lock_);
      status_ = response.status;
      terminal_ = terminal;
      complete_ = true;
    }
    changed_.notify_all();
  }

  bool wait_for_terminal() noexcept {
    std::unique_lock guard(lock_);
    return changed_.wait_for(
               guard, std::chrono::seconds(5), [this] { return complete_; }) &&
           status_ == 502 && terminal_;
  }

private:
  std::mutex lock_;
  std::condition_variable changed_;
  unsigned status_ = 0;
  bool terminal_ = false;
  bool complete_ = false;
};

class closing_observer final
    : public ntl::net::http3::proxy_connection_observer {
public:
  void on_inspection(
      const ntl::net::http::inspection_context_view &context) noexcept override {
    if (context.stage() != ntl::net::http::inspection_stage::headers ||
        calls.fetch_add(1, std::memory_order_acq_rel) != 0)
      return;
    if (const auto retained = owner.lock())
      retained->close();
  }

  std::weak_ptr<ntl::net::http3::proxy_connection> owner;
  std::atomic<unsigned> calls{0};
};

std::vector<std::byte> request_wire(
    std::string_view authority,
    std::string_view method = "GET",
    std::optional<std::string_view> protocol = std::nullopt) {
  std::vector<ntl::net::http3::header_field> fields;
  fields.push_back({":method", std::string(method), false});
  if (protocol)
    fields.push_back({":protocol", std::string(*protocol), false});
  fields.push_back({":scheme", "https", false});
  fields.push_back({":authority", std::string(authority), false});
  fields.push_back({":path", "/", false});
  ntl::net::http3::bounded_static_qpack_encoder encoder;
  auto block = encoder.encode(fields, 16 * 1024);
  if (!block)
    return {};
  std::vector<std::byte> wire;
  const ntl::status encoded =
      ntl::net::http3::webtransport::session_detail::append_frame(
          wire,
          static_cast<std::uint64_t>(
              ntl::net::http3::frame_type::headers),
          *block);
  return encoded.is_ok() ? wire : std::vector<std::byte>{};
}

std::vector<std::byte> dynamic_request_wire(std::string_view authority) {
  const std::vector<ntl::net::http3::header_field> fields{
      {":method", "GET", false},
      {":scheme", "https", false},
      {":authority", std::string(authority), false},
      {":path", "/", false}};
  ntl::net::http3::bounded_static_qpack_encoder encoder;
  auto block = encoder.encode(fields, 16 * 1024);
  if (!block || block->size() < 2)
    return {};
  // Require Insert Count=1 and append a dynamic relative index 0. The
  // request must remain blocked until the independent encoder stream arrives.
  (*block)[0] = std::byte{0x02};
  (*block)[1] = std::byte{0x00};
  block->push_back(std::byte{0x80});
  std::vector<std::byte> wire;
  const ntl::status encoded =
      ntl::net::http3::webtransport::session_detail::append_frame(
          wire,
          static_cast<std::uint64_t>(
              ntl::net::http3::frame_type::headers),
          *block);
  return encoded.is_ok() ? wire : std::vector<std::byte>{};
}

bool authority_case(
    std::optional<std::string> server_name,
    std::string_view authority, bool expected_permit) {
  auto backend_owner = std::make_shared<recording_backend>();
  auto &backend = *backend_owner;
  auto origin = std::make_shared<counting_origin>();
  auto async = std::make_shared<
      ntl::net::http3::immediate_origin_transport_adapter>(origin);
  auto policy = std::make_shared<ntl::net::http::inspection_policy>();
  auto decoders = std::make_shared<
      ntl::net::inspection::content_decoder_registry>();
  auto encoders = std::make_shared<
      ntl::net::inspection::content_encoder_registry>();
  policy->use_content_codecs(decoders, encoders);
  auto observer = std::make_shared<stage_observer>();
  ntl::net::http::inspection_session_metadata metadata;
  metadata.tls.server_name = std::move(server_name);
  metadata.tls.alpn = "h3";
  const std::string diagnostic_sni =
      metadata.tls.server_name.value_or("<missing>");
  auto proxy_owner = ntl::net::http3::proxy_connection::create(
      backend_owner, async, policy, std::move(metadata), observer, nullptr,
      nullptr,
      {.require_http3_origin = true,
       .require_server_name_authority_binding = true,
       .enable_webtransport = false});
  if (!proxy_owner)
    return false;
  auto &proxy = **proxy_owner;
  if (!proxy.on_connected("h3").is_ok())
    return false;
  const auto wire = request_wire(authority);
  if (wire.empty())
    return false;
  const ntl::status consumed = proxy.on_request_stream(
      0,
      ntl::net::scatter_view::from_contiguous(
          std::span<const std::byte>(wire)),
      true);
  const auto calls = origin->calls.load(std::memory_order_relaxed);
  const bool valid = consumed.is_ok() &&
                     calls == (expected_permit ? 1u : 0u) &&
                     backend.resets == (expected_permit ? 0u : 1u) &&
                     observer->request_headers == 1 &&
                     observer->request_body_chunks == 0 &&
                     observer->request_complete == 1 &&
                     observer->response_headers ==
                         (expected_permit ? 1u : 0u) &&
                     observer->response_body_chunks == 0 &&
                     observer->response_complete ==
                         (expected_permit ? 1u : 0u);
  if (!valid) {
    std::cerr << "authority case failed sni="
              << diagnostic_sni
              << " authority=" << authority
              << " status="
              << static_cast<unsigned long>(
                     static_cast<NTSTATUS>(consumed))
              << " calls=" << calls
              << " resets=" << backend.resets
              << " request-stages=" << observer->request_headers << '/'
              << observer->request_body_chunks << '/'
              << observer->request_complete
              << " response-stages=" << observer->response_headers << '/'
              << observer->response_body_chunks << '/'
              << observer->response_complete << '\n';
  }
  return valid;
}

bool test_strict_sni_authority_binding() {
  const bool dns =
      authority_case("Example.Test", "example.test", true);
  const bool default_port = authority_case(
      "example.test.", "EXAMPLE.TEST:443", true);
  const bool wrong_port = authority_case(
      "example.test", "example.test:444", false);
  const bool missing_sni =
      authority_case(std::nullopt, "example.test", false);
  const bool bracketed_ipv6 = authority_case(
      "2001:db8::1", "[2001:DB8::1]:443", true);
  const bool unbracketed_ipv6 = authority_case(
      "2001:db8::1", "2001:db8::1", false);
  if (!(dns && default_port && wrong_port && missing_sni &&
        bracketed_ipv6 && unbracketed_ipv6)) {
    std::cerr << "authority cases: dns=" << dns
              << " default-port=" << default_port
              << " wrong-port=" << wrong_port
              << " missing-sni=" << missing_sni
              << " bracketed-ipv6=" << bracketed_ipv6
              << " unbracketed-ipv6=" << unbracketed_ipv6 << '\n';
  }
  return dns && default_port && wrong_port && missing_sni &&
         bracketed_ipv6 && unbracketed_ipv6;
}

bool test_unknown_extended_connect_is_terminal() {
  auto backend_owner = std::make_shared<recording_backend>();
  auto &backend = *backend_owner;
  auto origin = std::make_shared<counting_origin>();
  auto async = std::make_shared<
      ntl::net::http3::immediate_origin_transport_adapter>(origin);
  auto policy = std::make_shared<ntl::net::http::inspection_policy>();
  auto decoders = std::make_shared<
      ntl::net::inspection::content_decoder_registry>();
  auto encoders = std::make_shared<
      ntl::net::inspection::content_encoder_registry>();
  policy->use_content_codecs(decoders, encoders);
  ntl::net::http::inspection_session_metadata metadata;
  metadata.tls.server_name = "example.test";
  metadata.tls.alpn = "h3";
  auto proxy_owner = ntl::net::http3::proxy_connection::create(
      backend_owner, async, policy, std::move(metadata), nullptr, nullptr,
      nullptr,
      {.require_http3_origin = true,
       .require_server_name_authority_binding = true,
       .enable_webtransport = true});
  if (!proxy_owner)
    return false;
  auto &proxy = **proxy_owner;
  if (!proxy.on_connected("h3").is_ok())
    return false;
  const auto wire = request_wire(
      "example.test", "CONNECT", "connect-udp");
  if (wire.empty() ||
      !proxy
           .on_request_stream(
               0,
               ntl::net::scatter_view::from_contiguous(
                   std::span<const std::byte>(wire)),
               true)
           .is_ok())
    return false;
  return origin->calls.load(std::memory_order_relaxed) == 0 &&
         backend.resets == 0 && backend.writes.size() > 1;
}

bool test_async_pool_namespaces_stream_ids() {
  auto origin = std::make_shared<blocking_origin>();
  ntl::net::http3::async_origin_pool pool(
      origin,
      {.maximum_concurrency = 2,
       .maximum_outstanding_requests = 8});
  auto first = pool.make_transport();
  auto second = pool.make_transport();
  std::mutex lock;
  std::condition_variable changed;
  std::size_t completions = 0;
  const auto completion = [&](ntl::result<
                                  ntl::net::http3::origin_response>
                                  response) {
    std::lock_guard guard(lock);
    if (response)
      ++completions;
    changed.notify_all();
  };
  ntl::net::http3::origin_request request;
  request.server_name = "example.test";
  request.authority = "example.test";
  if (!first->submit(0, request, completion).is_ok() ||
      !second->submit(0, request, completion).is_ok())
    return false;
  if (!origin->wait_entered(2)) {
    origin->release();
    return false;
  }

  // The same stream ID is invalid only within one connection-local channel.
  const ntl::status duplicate = first->submit(0, request, completion);
  origin->release();
  if (duplicate != STATUS_OBJECT_NAME_COLLISION)
    return false;
  {
    std::unique_lock guard(lock);
    if (!changed.wait_for(
            guard, std::chrono::seconds(5),
            [&] { return completions == 2; }))
      return false;
  }
  pool.stop_accepting();
  const auto statistics = pool.statistics();
  return pool.drain().is_ok() && statistics.submitted == 2 &&
         origin->entered == 2;
}

bool test_proxy_close_from_callback() {
  auto backend = std::make_shared<recording_backend>();
  auto origin = std::make_shared<counting_origin>();
  auto async = std::make_shared<
      ntl::net::http3::immediate_origin_transport_adapter>(origin);
  auto policy = std::make_shared<ntl::net::http::inspection_policy>();
  auto decoders = std::make_shared<
      ntl::net::inspection::content_decoder_registry>();
  auto encoders = std::make_shared<
      ntl::net::inspection::content_encoder_registry>();
  policy->use_content_codecs(decoders, encoders);
  auto observer = std::make_shared<closing_observer>();
  ntl::net::http::inspection_session_metadata metadata;
  metadata.tls.server_name = "example.test";
  metadata.tls.alpn = "h3";
  auto created = ntl::net::http3::proxy_connection::create(
      backend, async, policy, std::move(metadata), observer, nullptr,
      nullptr,
      {.require_http3_origin = true,
       .require_server_name_authority_binding = true,
       .enable_webtransport = false});
  if (!created)
    return false;
  auto owner = std::move(*created);
  observer->owner = owner;
  if (!owner->on_connected("h3").is_ok())
    return false;
  const auto wire = request_wire("example.test");
  if (wire.empty())
    return false;
  const ntl::status accepted = owner->on_request_stream(
      0, ntl::net::scatter_view::from_contiguous(wire), true);
  const ntl::status rejected = owner->on_request_stream(
      4, ntl::net::scatter_view::from_contiguous(wire), true);
  owner->close();
  owner->close();
  return accepted.is_ok() && observer->calls.load(std::memory_order_acquire) != 0 &&
         static_cast<NTSTATUS>(rejected) == STATUS_DELETE_PENDING;
}

bool test_origin_failure_returns_bad_gateway() {
  auto backend = std::make_shared<recording_backend>();
  auto origin = std::make_shared<failing_origin>();
  auto async = std::make_shared<
      ntl::net::http3::immediate_origin_transport_adapter>(origin);
  auto policy = std::make_shared<ntl::net::http::inspection_policy>();
  auto decoders = std::make_shared<
      ntl::net::inspection::content_decoder_registry>();
  auto encoders = std::make_shared<
      ntl::net::inspection::content_encoder_registry>();
  policy->use_content_codecs(decoders, encoders);
  auto observer = std::make_shared<completion_observer>();
  ntl::net::http::inspection_session_metadata metadata;
  metadata.tls.server_name = "example.test";
  metadata.tls.alpn = "h3";
  auto created = ntl::net::http3::proxy_connection::create(
      backend, async, policy, std::move(metadata), observer, nullptr,
      nullptr,
      {.require_http3_origin = false,
       .require_server_name_authority_binding = true,
       .enable_webtransport = false});
  if (!created || !(*created)->on_connected("h3").is_ok())
    return false;
  const auto wire = dynamic_request_wire("example.test");
  if (wire.empty() ||
      !(*created)
           ->on_request_stream(
               0, ntl::net::scatter_view::from_contiguous(wire), true)
           .is_ok())
    return false;
  constexpr std::array<std::byte, 6> encoder_instructions{
      std::byte{0x3f}, std::byte{0x21}, std::byte{0x41},
      std::byte{0x78}, std::byte{0x01}, std::byte{0x79}};
  if (!(*created)
           ->on_qpack_encoder_stream(
               ntl::net::scatter_view::from_contiguous(
                   std::span<const std::byte>(encoder_instructions)))
           .is_ok())
    return false;
  return observer->completions == 1 && observer->status == 502 &&
         observer->terminal_response && backend->resets == 0 &&
         backend->final_writes.load(std::memory_order_acquire) == 1;
}

bool test_async_origin_failure_finishes_terminal_stream() {
  auto backend = std::make_shared<recording_backend>();
  auto origin = std::make_shared<failing_origin>();
  ntl::net::http3::async_origin_pool pool(
      origin, {.maximum_concurrency = 1,
               .maximum_outstanding_requests = 4});
  auto policy = std::make_shared<ntl::net::http::inspection_policy>();
  auto decoders = std::make_shared<
      ntl::net::inspection::content_decoder_registry>();
  auto encoders = std::make_shared<
      ntl::net::inspection::content_encoder_registry>();
  policy->use_content_codecs(decoders, encoders);
  auto observer = std::make_shared<waiting_terminal_observer>();
  ntl::net::http::inspection_session_metadata metadata;
  metadata.tls.server_name = "example.test";
  metadata.tls.alpn = "h3";
  auto created = ntl::net::http3::proxy_connection::create(
      backend, pool.make_transport(), policy, std::move(metadata), observer,
      nullptr, nullptr,
      {.require_http3_origin = false,
       .require_server_name_authority_binding = true,
       .enable_webtransport = false});
  if (!created || !(*created)->on_connected("h3").is_ok())
    return false;
  const auto wire = dynamic_request_wire("example.test");
  if (wire.empty() ||
      !(*created)
           ->on_request_stream(
               0, ntl::net::scatter_view::from_contiguous(wire), true)
           .is_ok())
    return false;
  constexpr std::array<std::byte, 6> encoder_instructions{
      std::byte{0x3f}, std::byte{0x21}, std::byte{0x41},
      std::byte{0x78}, std::byte{0x01}, std::byte{0x79}};
  if (!(*created)
           ->on_qpack_encoder_stream(
               ntl::net::scatter_view::from_contiguous(
                   std::span<const std::byte>(encoder_instructions)))
           .is_ok())
    return false;
  const bool complete = observer->wait_for_terminal();
  const auto statistics = pool.statistics();
  (*created)->close();
  pool.close();
  return complete && statistics.completed == 1 &&
         backend->resets == 0 &&
         backend->final_writes.load(std::memory_order_acquire) == 1;
}

bool test_proxy_close_use_race_and_owner_first_release() {
  auto backend = std::make_shared<recording_backend>();
  auto origin = std::make_shared<counting_origin>();
  auto async = std::make_shared<
      ntl::net::http3::immediate_origin_transport_adapter>(origin);
  auto policy = std::make_shared<ntl::net::http::inspection_policy>();
  auto decoders = std::make_shared<
      ntl::net::inspection::content_decoder_registry>();
  auto encoders = std::make_shared<
      ntl::net::inspection::content_encoder_registry>();
  policy->use_content_codecs(decoders, encoders);
  ntl::net::http::inspection_session_metadata metadata;
  metadata.tls.server_name = "example.test";
  metadata.tls.alpn = "h3";
  auto created = ntl::net::http3::proxy_connection::create(
      backend, async, policy, std::move(metadata), nullptr, nullptr,
      nullptr,
      {.require_http3_origin = true,
       .require_server_name_authority_binding = true,
       .enable_webtransport = false});
  if (!created)
    return false;
  auto owner = std::move(*created);
  if (!owner->on_connected("h3").is_ok())
    return false;
  const auto wire = request_wire("example.test");
  if (wire.empty())
    return false;

  std::weak_ptr<ntl::net::http3::proxy_connection> lifetime = owner;
  std::atomic<bool> ready{false};
  std::atomic<bool> start{false};
  std::atomic<bool> rejected{false};
  std::atomic<bool> unexpected{false};
  std::thread user(
      [retained = owner, &wire, &ready, &start, &rejected, &unexpected] {
        ready.store(true, std::memory_order_release);
        while (!start.load(std::memory_order_acquire))
          std::this_thread::yield();
        for (std::uint64_t stream_id = 0;; stream_id += 4) {
          const ntl::status status = retained->on_request_stream(
              stream_id, ntl::net::scatter_view::from_contiguous(wire), true);
          if (static_cast<NTSTATUS>(status) == STATUS_DELETE_PENDING) {
            rejected.store(true, std::memory_order_release);
            return;
          }
          if (!status.is_ok()) {
            unexpected.store(true, std::memory_order_release);
            return;
          }
        }
      });
  while (!ready.load(std::memory_order_acquire))
    std::this_thread::yield();
  start.store(true, std::memory_order_release);
  owner->close();
  owner->close();
  owner.reset();
  user.join();
  return rejected.load(std::memory_order_acquire) &&
         !unexpected.load(std::memory_order_acquire) && lifetime.expired();
}

bool test_proxy_run_retains_owner_until_backend_drains() {
  auto backend = std::make_shared<blocking_run_backend>();
  auto origin = std::make_shared<counting_origin>();
  auto async = std::make_shared<
      ntl::net::http3::immediate_origin_transport_adapter>(origin);
  auto policy = std::make_shared<ntl::net::http::inspection_policy>();
  auto decoders = std::make_shared<
      ntl::net::inspection::content_decoder_registry>();
  auto encoders = std::make_shared<
      ntl::net::inspection::content_encoder_registry>();
  policy->use_content_codecs(decoders, encoders);
  auto created = ntl::net::http3::proxy_connection::create(
      backend, async, policy);
  if (!created)
    return false;

  auto owner = std::move(*created);
  std::weak_ptr<ntl::net::http3::proxy_connection> lifetime = owner;
  auto *const borrowed = owner.get();
  ntl::status terminal = STATUS_UNSUCCESSFUL;
  std::thread runner([&] { terminal = borrowed->run(); });
  if (!backend->wait_entered()) {
    backend->release();
    runner.join();
    return false;
  }

  owner.reset();
  const bool retained_while_running = !lifetime.expired();
  backend->release();
  runner.join();
  return retained_while_running && terminal.is_ok() && lifetime.expired();
}

bool test_async_pool_owner_first_close() {
  auto origin = std::make_shared<blocking_origin>();
  auto pool = std::make_unique<ntl::net::http3::async_origin_pool>(
      origin, ntl::net::http3::async_origin_pool_limits{
                  .maximum_concurrency = 1,
                  .maximum_outstanding_requests = 2});
  auto transport = pool->make_transport();
  std::mutex lock;
  std::condition_variable changed;
  bool completed = false;
  ntl::net::http3::origin_request request;
  request.server_name = "example.test";
  request.authority = "example.test";
  const ntl::status submitted = transport->submit(
      7, request,
      [&](ntl::result<ntl::net::http3::origin_response>) {
        std::lock_guard guard(lock);
        pool.reset();
        completed = true;
        changed.notify_all();
      });
  if (!submitted.is_ok() || !origin->wait_entered(1)) {
    origin->release();
    return false;
  }
  origin->release();
  {
    std::unique_lock guard(lock);
    if (!changed.wait_for(guard, std::chrono::seconds(5),
                          [&] { return completed; }) || pool)
      return false;
  }
  const ntl::status rejected = transport->submit(
      8, request,
      [](ntl::result<ntl::net::http3::origin_response>) {});
  return static_cast<NTSTATUS>(rejected) == STATUS_DELETE_PENDING;
}

bool test_async_pool_last_release_from_callback() {
  auto *const cleanup =
      ntl::net::user::borrowed_runtime_cleanup_domain();
  if (!cleanup)
    return false;
  auto origin = std::make_shared<blocking_origin>();
  auto pool = std::make_unique<ntl::net::http3::async_origin_pool>(
      origin, ntl::net::http3::async_origin_pool_limits{
                  .maximum_concurrency = 1,
                  .maximum_outstanding_requests = 1});
  auto transport = pool->make_transport();
  std::mutex lock;
  std::condition_variable changed;
  bool released = false;
  ntl::net::http3::origin_request request;
  request.server_name = "example.test";
  request.authority = "example.test";
  const ntl::status submitted = transport->submit(
      9, request,
      [&](ntl::result<ntl::net::http3::origin_response>) {
        pool.reset();
        transport.reset();
        {
          std::lock_guard guard(lock);
          released = true;
        }
        changed.notify_all();
      });
  if (!submitted.is_ok() || !origin->wait_entered(1)) {
    origin->release();
    return false;
  }
  origin->release();
  {
    std::unique_lock guard(lock);
    if (!changed.wait_for(guard, std::chrono::seconds(5),
                          [&] { return released; }))
      return false;
  }
  if (pool || transport || !cleanup->drain().is_ok())
    return false;
  return true;
}

bool test_async_pool_same_facade_close_use_race() {
  auto origin = std::make_shared<counting_origin>();
  ntl::net::http3::async_origin_pool pool(
      origin, {.maximum_concurrency = 1,
               .maximum_outstanding_requests = 4});
  std::atomic<bool> start{false};
  std::atomic<unsigned> created{0};
  std::atomic<bool> rejected{false};
  std::atomic<bool> unexpected{false};
  std::thread user([&] {
    while (!start.load(std::memory_order_acquire))
      std::this_thread::yield();
    for (;;) {
      try {
        auto transport = pool.make_transport();
        if (!transport) {
          unexpected.store(true, std::memory_order_release);
          return;
        }
        created.fetch_add(1, std::memory_order_release);
      } catch (const std::logic_error &) {
        rejected.store(true, std::memory_order_release);
        return;
      } catch (...) {
        unexpected.store(true, std::memory_order_release);
        return;
      }
    }
  });
  start.store(true, std::memory_order_release);
  while (created.load(std::memory_order_acquire) == 0)
    std::this_thread::yield();
  pool.close();
  pool.close();
  user.join();
  return rejected.load(std::memory_order_acquire) &&
         !unexpected.load(std::memory_order_acquire);
}

bool test_async_pool_byte_quota_is_fail_closed() {
  auto origin = std::make_shared<counting_origin>();
  ntl::net::http3::async_origin_pool pool(
      origin, {.maximum_concurrency = 1,
               .maximum_outstanding_requests = 2,
               .maximum_buffered_request_bytes = 96});
  auto transport = pool.make_transport();
  ntl::net::http3::origin_request request;
  request.server_name = "example.test";
  request.authority = "example.test";
  request.path = "/oversized";
  request.body.resize(128, std::byte{0x41});
  const ntl::status rejected = transport->submit(
      17, std::move(request), [](auto) noexcept {});
  const auto statistics = pool.statistics();
  pool.close();
  return rejected == STATUS_QUOTA_EXCEEDED &&
         statistics.submitted == 0 &&
         statistics.buffered_request_bytes == 0 &&
         statistics.byte_quota_rejections == 1 && origin->calls == 0;
}

bool test_cleanup_domain_duplicate_retire_and_close() {
  struct cleanup_probe {
    static void run(void *context) noexcept {
      static_cast<std::atomic<unsigned> *>(context)->fetch_add(
          1, std::memory_order_acq_rel);
    }
  };

  std::atomic<unsigned> cleanups{0};
  ntl::net::user::cleanup_domain domain;
  ntl::net::user::cleanup_item item(&cleanup_probe::run, &cleanups);
  if (!domain.retire_deferred(item).is_ok() ||
      !domain.retire_deferred(item).is_ok() ||
      !domain.drain().is_ok() || cleanups.load(std::memory_order_acquire) != 1)
    return false;
  return domain.stop_and_drain().is_ok() &&
         domain.stop_and_drain().is_ok() && domain.live() == 0;
}

} // namespace

int main() {
  const bool authority = test_strict_sni_authority_binding();
  const bool extended_connect =
      test_unknown_extended_connect_is_terminal();
  const bool callback_close = test_proxy_close_from_callback();
  const bool origin_failure = test_origin_failure_returns_bad_gateway();
  const bool async_origin_failure =
      test_async_origin_failure_finishes_terminal_stream();
  const bool close_race =
      test_proxy_close_use_race_and_owner_first_release();
  const bool run_lifetime =
      test_proxy_run_retains_owner_until_backend_drains();
  const bool pool = test_async_pool_namespaces_stream_ids();
  const bool pool_owner_first = test_async_pool_owner_first_close();
  const bool pool_callback_release =
      test_async_pool_last_release_from_callback();
  const bool pool_close_race =
      test_async_pool_same_facade_close_use_race();
  const bool pool_byte_quota =
      test_async_pool_byte_quota_is_fail_closed();
  const bool cleanup_domain =
      test_cleanup_domain_duplicate_retire_and_close();
  if (!authority || !extended_connect || !callback_close || !origin_failure ||
      !async_origin_failure ||
      !close_race ||
      !run_lifetime ||
      !pool || !pool_owner_first ||
      !pool_callback_release || !pool_close_race || !pool_byte_quota ||
      !cleanup_domain) {
    std::cerr << "contract groups: authority=" << authority
              << " extended-connect=" << extended_connect
              << " callback-close=" << callback_close
              << " origin-failure=" << origin_failure
              << " async-origin-failure=" << async_origin_failure
              << " close-race=" << close_race
              << " run-lifetime=" << run_lifetime
              << " async-pool=" << pool
              << " pool-owner-first=" << pool_owner_first
              << " pool-callback-release=" << pool_callback_release
              << " pool-close-race=" << pool_close_race
              << " pool-byte-quota=" << pool_byte_quota
              << " cleanup-domain=" << cleanup_domain << '\n';
    std::cerr << "HTTP/3 proxy connection contracts failed\n";
    return 1;
  }
  std::cout << "HTTP/3 proxy connection contracts passed\n";
  return 0;
}
