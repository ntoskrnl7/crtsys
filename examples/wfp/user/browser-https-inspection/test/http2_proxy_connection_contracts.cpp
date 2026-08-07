#include <ntl/net/http/inspection_policy>
#include <ntl/net/http2/proxy_connection>
#include <ntl/net/http2/proxy_session>
#include <ntl/net/inspection/content_decoder>
#include <ntl/net/inspection/content_encoder>

#include <array>
#include <coroutine>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <memory>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

using ntl::net::http2::connection_direction;
using ntl::net::http2::outbound_frame;

void require(bool condition, std::string_view message) {
  if (!condition)
    throw std::runtime_error(std::string(message));
}

std::vector<std::byte> bytes(std::string_view text) {
  const auto view = std::as_bytes(std::span(text));
  return {view.begin(), view.end()};
}

outbound_frame settings(
    bool enable_connect = false,
    std::uint32_t initial_window =
        ntl::net::http2::send_window::default_window,
    std::uint32_t maximum_frame_size = 0) {
  std::vector<std::byte> payload;
  const auto append = [&payload](std::uint16_t identifier,
                                 std::uint32_t value) {
    payload.push_back(static_cast<std::byte>((identifier >> 8) & 0xffu));
    payload.push_back(static_cast<std::byte>(identifier & 0xffu));
    payload.push_back(static_cast<std::byte>((value >> 24) & 0xffu));
    payload.push_back(static_cast<std::byte>((value >> 16) & 0xffu));
    payload.push_back(static_cast<std::byte>((value >> 8) & 0xffu));
    payload.push_back(static_cast<std::byte>(value & 0xffu));
  };
  append(0x4u, initial_window);
  if (enable_connect)
    append(0x8u, 1);
  if (maximum_frame_size != 0)
    append(0x5u, maximum_frame_size);
  auto encoded = ntl::net::http2::transform_detail::make_frame(
      ntl::net::http2::frame_type::settings, 0, 0, payload);
  require(encoded.has_value(), "could not encode SETTINGS");
  return std::move(*encoded);
}

outbound_frame settings_acknowledgement() {
  auto encoded = ntl::net::http2::transform_detail::make_frame(
      ntl::net::http2::frame_type::settings, 0x01u, 0, {});
  require(encoded.has_value(), "could not encode SETTINGS acknowledgement");
  return std::move(*encoded);
}

outbound_frame control_frame(
    ntl::net::http2::frame_type type, std::uint32_t stream_id,
    std::array<std::byte, 4> payload) {
  auto encoded = ntl::net::http2::transform_detail::make_frame(
      type, 0, stream_id, payload);
  require(encoded.has_value(), "could not encode control frame");
  return std::move(*encoded);
}

outbound_frame goaway(std::uint32_t last_stream_id,
                      std::uint32_t error_code) {
  std::array<std::byte, 8> payload{
      static_cast<std::byte>((last_stream_id >> 24) & 0x7fu),
      static_cast<std::byte>((last_stream_id >> 16) & 0xffu),
      static_cast<std::byte>((last_stream_id >> 8) & 0xffu),
      static_cast<std::byte>(last_stream_id & 0xffu),
      static_cast<std::byte>((error_code >> 24) & 0xffu),
      static_cast<std::byte>((error_code >> 16) & 0xffu),
      static_cast<std::byte>((error_code >> 8) & 0xffu),
      static_cast<std::byte>(error_code & 0xffu)};
  auto encoded = ntl::net::http2::transform_detail::make_frame(
      ntl::net::http2::frame_type::goaway, 0, 0, payload);
  require(encoded.has_value(), "could not encode GOAWAY");
  return std::move(*encoded);
}

ntl::net::http2::frame_view parse(const outbound_frame &wire) {
  auto parsed = ntl::net::http2::frame_view::parse(
      ntl::net::scatter_view::from_contiguous(wire.wire),
      {1024 * 1024, false});
  require(parsed.has_value(), "could not parse generated frame");
  return *parsed;
}

ntl::net::http::request_message request(
    std::string path, std::string method = "GET") {
  ntl::net::http::request_message value;
  value.wire_protocol = ntl::net::http::protocol::http2;
  value.method = std::move(method);
  value.scheme = "https";
  value.authority = "policy.example.test";
  value.path = std::move(path);
  return value;
}

struct observation {
  void on_inspection(
      const ntl::net::http::inspection_context_view &context) noexcept {
    ++calls;
    stages.push_back(context.stage());
    metadata_ok = metadata_ok && context.wire_protocol() ==
                                       ntl::net::http::protocol::http2 &&
                  context.stream_id() == context.exchange_id() &&
                  context.connection().flow_id == 77 &&
                  context.connection().process_id == 101 &&
                   context.connection().application_label == "browser.exe" &&
                   context.tls().server_name == "policy.example.test" &&
                   context.tls().alpn == "h2";
    if (context.direction() ==
        ntl::net::http::message_direction::request) {
      request_context_ok = request_context_ok &&
                           context.method() != std::string_view{} &&
                           context.scheme() == "https" &&
                           context.authority() == "policy.example.test";
    } else {
      response_associated = response_associated &&
                            context.response() != nullptr &&
                            context.request().authority ==
                                "policy.example.test";
    }
  }

  unsigned calls = 0;
  bool metadata_ok = true;
  bool request_context_ok = true;
  bool response_associated = true;
  std::vector<ntl::net::http::inspection_stage> stages;
};

struct close_on_observation {
  void on_inspection(
      const ntl::net::http::inspection_context_view &) noexcept {
    if (auto retained = owner.lock())
      retained->close();
  }

  std::weak_ptr<ntl::net::http2::proxy_connection> owner;
};

ntl::net::http::inspection_policy make_policy(std::size_t maximum_body) {
  ntl::net::http::inspection_policy policy(
      {.maximum_header_count = 64,
       .maximum_header_bytes = 16 * 1024,
       .maximum_encoded_body_bytes = maximum_body,
       .maximum_decoded_body_bytes = maximum_body,
       .maximum_expansion_ratio = 8,
       .maximum_coding_layers = 2,
       .on_failure =
           ntl::net::http::transform_failure_policy::block});
  policy.transforms_ref().requests().transform(
      [](ntl::net::http::request_message &value) {
        value.headers.set("x-ntl-http2-session", "1");
        return ntl::net::http::rewrite_result::headers_changed();
      });
  policy.transforms_ref().responses()
      .when([](const ntl::net::http::request_message &,
               const ntl::net::http::response_message &value) {
        return value.status == 103;
      })
      .transform([](const ntl::net::http::request_message &,
                    ntl::net::http::response_message &value) {
        value.headers.set("x-ntl-informational-inspected", "1");
        return ntl::net::http::rewrite_result::headers_changed();
      });
  policy.requests()
      .at_headers()
      .when([](const ntl::net::http::inspection_context_view &context) {
        return context.method() == "GET" && context.path() == "/blocked" &&
               context.query() == "reason=header" &&
               context.headers().first("x-deny") == "1" &&
               context.connection().application_label == "browser.exe" &&
               context.tls().alpn == "h2";
      })
      .decide([](const ntl::net::http::inspection_context_view &) {
        return ntl::net::inspection::verdict::block;
      });
  policy.requests()
      .at_body_chunk()
      .when([](const ntl::net::http::inspection_context_view &context) {
        const auto body = context.body_chunk();
        const std::string_view text(
            reinterpret_cast<const char *>(body.data()), body.size());
        return text.find("DROP") != std::string_view::npos;
      })
      .decide([](const ntl::net::http::inspection_context_view &) {
        return ntl::net::inspection::verdict::drop_flow;
      });
  policy.responses()
      .at_message_complete()
      .when([](const ntl::net::http::inspection_context_view &context) {
        return context.response() && context.response()->status == 451;
      })
      .decide([](const ntl::net::http::inspection_context_view &) {
        return ntl::net::inspection::verdict::block;
      });
  return policy;
}

struct fixture {
  static std::shared_ptr<ntl::net::http::inspection_policy>
  make_owned_policy(
      std::size_t maximum_body,
      const std::shared_ptr<
          ntl::net::inspection::content_decoder_registry> &decoders,
      const std::shared_ptr<
          ntl::net::inspection::content_encoder_registry> &encoders) {
    auto result = std::make_shared<ntl::net::http::inspection_policy>(
        make_policy(maximum_body));
    result->use_content_codecs(decoders, encoders);
    return result;
  }

  explicit fixture(std::size_t maximum_body = 4096)
      : policy(make_owned_policy(maximum_body, decoders, encoders)),
        connection(
            policy, {.connection =
                 {.flow_id = 77,
                  .flow_direction =
                      ntl::net::inspection::direction::outbound,
                   .source = ntl::net::http::endpoint_metadata{
                       "127.0.0.1", 51000},
                   .destination = ntl::net::http::endpoint_metadata{
                       "127.0.0.1", 8443},
                   .original_source = ntl::net::http::endpoint_metadata{
                       "10.0.0.5", 51000},
                   .original_destination = ntl::net::http::endpoint_metadata{
                       "203.0.113.5", 443},
                  .process_id = 101,
                   .application_label = "browser.exe"},
             .tls = {.server_name = "policy.example.test",
                      .alpn = "h2"}},
            ntl::net::http2::inspection_observer(observer),
            {.require_server_name_authority_binding = true}) {
    require(connection.accept_client_preface(
                ntl::net::http2::client_connection_preface)
                .is_ok(),
            "valid client preface was rejected");
    const auto client_settings = settings();
    const auto server_settings = settings(true);
    require(connection.consume(connection_direction::requests,
                               parse(client_settings))
                .has_value(),
            "client SETTINGS was rejected");
    require(connection.consume(connection_direction::responses,
                               parse(server_settings))
                .has_value(),
            "server SETTINGS was rejected");
  }

  std::shared_ptr<ntl::net::inspection::content_decoder_registry> decoders =
      std::make_shared<ntl::net::inspection::content_decoder_registry>();
  std::shared_ptr<ntl::net::inspection::content_encoder_registry> encoders =
      std::make_shared<ntl::net::inspection::content_encoder_registry>();
  std::shared_ptr<ntl::net::http::inspection_policy> policy;
  std::shared_ptr<observation> observer = std::make_shared<observation>();
  ntl::net::http2::proxy_connection connection;
};

ntl::net::http2::proxy_connection_step feed_request(
    fixture &state, std::uint32_t stream_id,
    const ntl::net::http::request_message &message,
    std::span<const std::byte> body = {}, bool end_stream = true,
    std::size_t header_fragment = 16) {
  auto encoded = ntl::net::http2::encode_request_frames(
      stream_id, message, body, header_fragment, 16 * 1024, end_stream);
  if (!encoded)
    throw std::runtime_error(
        "request frame encoding failed: " +
        std::to_string(static_cast<unsigned>(encoded.status())));
  ntl::net::http2::proxy_connection_step completed;
  for (const auto &wire : *encoded) {
    auto step = state.connection.consume(
        connection_direction::requests, parse(wire));
    require(step.has_value(), "request frame was rejected");
    if (step->message_complete)
      completed = std::move(*step);
  }
  require(completed.message_complete, "request did not complete");
  return completed;
}

ntl::net::http2::proxy_connection_step feed_response(
    fixture &state, std::uint32_t stream_id,
    const ntl::net::http::response_message &message,
    std::span<const std::byte> body = {}, bool body_forbidden = false,
    bool end_stream = true) {
  auto encoded = ntl::net::http2::encode_response_frames(
      stream_id, message, body, 16, 16 * 1024, body_forbidden, end_stream);
  require(encoded.has_value(), "response frame encoding failed");
  ntl::net::http2::proxy_connection_step completed;
  for (const auto &wire : *encoded) {
    auto step = state.connection.consume(
        connection_direction::responses, parse(wire));
    require(step.has_value(), "response frame was rejected");
    if (step->message_complete)
      completed = std::move(*step);
  }
  require(completed.message_complete, "response did not complete");
  return completed;
}

void test_context_continuation_flow_and_response_association() {
  fixture state;
  auto message = request("/inspect?q=one", "POST");
  const auto body = bytes("request-body");
  message.headers.append("content-length", std::to_string(body.size()));
  message.headers.append("x-context", "present");
  const auto request_step = feed_request(state, 1, message, body, true, 8);
  require(request_step.request && !request_step.forward.empty() &&
              request_step.request->headers.first("x-ntl-http2-session") ==
                  "1" &&
              request_step.reverse.size() == 1,
          "request transform or source flow credit was not applied");

  ntl::net::http::response_message early;
  early.wire_protocol = ntl::net::http::protocol::http2;
  early.status = 103;
  auto informational = ntl::net::http2::encode_response_frames(
      1, early, {}, 16, 16 * 1024, true, false);
  require(informational.has_value(), "1xx response encoding failed");
  std::shared_ptr<const ntl::net::http::request_message>
      informational_request;
  bool informational_complete = false;
  for (const auto &wire : *informational) {
    auto step = state.connection.consume(
        connection_direction::responses, parse(wire));
    require(step.has_value(), "1xx response was rejected");
    if (!step->message_complete)
      continue;
    informational_complete = true;
    informational_request = step->associated_request;
    require(step->informational && step->response &&
                step->response->status == 103 &&
                step->response->headers.first(
                    "x-ntl-informational-inspected") == "1" &&
                !step->forward.empty(),
            "1xx response bypassed semantic inspection");
  }
  require(informational_complete && informational_request,
          "1xx response did not produce a semantic inspection event");

  ntl::net::http::response_message response;
  response.wire_protocol = ntl::net::http::protocol::http2;
  response.status = 200;
  const auto response_body = bytes("response-body");
  response.headers.append("content-type", "text/plain");
  response.headers.append(
      "content-length", std::to_string(response_body.size()));
  const auto response_step =
      feed_response(state, 1, response, response_body);
  require(response_step.response && response_step.forward.size() >= 2 &&
              response_step.associated_request == informational_request &&
              !response_step.informational && state.observer->calls == 8 &&
              state.observer->metadata_ok &&
              state.observer->request_context_ok &&
              state.observer->response_associated,
          "staged request/response context was incomplete");
}

void test_header_block_body_drop_and_head() {
  fixture blocked_state;
  auto denied = request("/blocked?reason=header");
  denied.headers.append("x-deny", "1");
  const auto blocked = feed_request(blocked_state, 3, denied);
  require(blocked.staged_verdict ==
              ntl::net::inspection::verdict::block &&
              blocked.terminal_status == 403 && blocked.forward.empty() &&
              !blocked.reverse.empty(),
          "full-context header policy did not block stream-locally");

  fixture dropped_state;
  auto upload = request("/upload", "POST");
  const auto forbidden = bytes("prefix-DROP-suffix");
  upload.headers.append("content-length", std::to_string(forbidden.size()));
  const auto dropped = feed_request(dropped_state, 5, upload, forbidden);
  require(dropped.drop_flow && dropped.forward.empty() &&
              dropped.reverse.empty(),
          "body-chunk policy did not request a flow drop");

  fixture head_state;
  auto head = request("/metadata", "HEAD");
  const auto head_request = feed_request(head_state, 7, head);
  require(head_request.request.has_value(), "HEAD request was not associated");
  ntl::net::http::response_message head_response;
  head_response.wire_protocol = ntl::net::http::protocol::http2;
  head_response.status = 200;
  head_response.headers.append("content-length", "1234");
  const auto head_result =
      feed_response(head_state, 7, head_response, {}, true);
  require(head_result.response && head_result.response->body.empty(),
          "HEAD response acquired a forbidden body");
}

void test_terminal_transform_and_empty_body_stages() {
  {
    fixture state;
    unsigned staged_decisions = 0;
    state.policy->transforms_ref().requests().transform(
        [](ntl::net::http::request_message &message) {
          if (message.path != "/terminal")
            return ntl::net::http::rewrite_result::unchanged();
          ntl::net::http::response_message response;
          response.wire_protocol = ntl::net::http::protocol::http2;
          response.status = 418;
          response.headers.append("content-type", "text/plain");
          response.body = bytes("teapot");
          response.headers.append("content-length", "6");
          return ntl::net::http::rewrite_result::respond(
              std::move(response));
        });
    state.policy->requests()
        .at_headers()
        .when([](const ntl::net::http::inspection_context_view &context) {
          return context.path() == "/terminal";
        })
        .decide([&staged_decisions](
                    const ntl::net::http::inspection_context_view &) {
          ++staged_decisions;
          return ntl::net::inspection::verdict::block;
        });
    const auto terminal = feed_request(state, 1, request("/terminal"));
    require(terminal.transform_action ==
                    ntl::net::http::rewrite_action::respond &&
                terminal.terminal_status == 418 && terminal.forward.empty() &&
                !terminal.reverse.empty() && staged_decisions == 0 &&
                state.observer->calls == 0,
            "HTTP/2 staged policy replaced a terminal transform response");
  }

  {
    fixture state;
    const auto empty = feed_request(state, 1, request("/empty"));
    require(empty.request.has_value() && state.observer->calls == 2 &&
                state.observer->stages ==
                    std::vector<ntl::net::http::inspection_stage>{
                        ntl::net::http::inspection_stage::headers,
                        ntl::net::http::inspection_stage::message_complete},
            "HTTP/2 empty semantic body emitted a body_chunk stage");
  }
}

void test_pseudo_headers_authority_and_request_retention() {
  ntl::net::inspection::content_decoder_registry decoders;
  using field = ntl::net::http2::header_field;

  const std::vector<field> duplicate_empty_method{
      {":method", "", false},
      {":method", "GET", false},
      {":scheme", "https", false},
      {":authority", "example.test", false},
      {":path", "/", false}};
  const auto duplicate = ntl::net::http2::parse_request(
      duplicate_empty_method, {}, {}, decoders);
  require(!duplicate && duplicate.status() == STATUS_DATA_ERROR,
          "an empty first pseudo-header bypassed duplicate detection");

  const std::vector<field> host_and_authority{
      {":method", "GET", false},
      {":scheme", "https", false},
      {":authority", "one.example.test", false},
      {":path", "/", false},
      {"host", "two.example.test", false}};
  const auto ambiguous = ntl::net::http2::parse_request(
      host_and_authority, {}, {}, decoders);
  require(!ambiguous && ambiguous.status() == STATUS_DATA_ERROR,
          "Host and :authority were accepted as two routing authorities");

  const std::vector<field> uppercase_request_header{
      {":method", "GET", false},
      {":scheme", "https", false},
      {":authority", "one.example.test", false},
      {":path", "/", false},
      {"X-Policy", "inspect", false}};
  const auto invalid_request_header = ntl::net::http2::parse_request(
      uppercase_request_header, {}, {}, decoders);
  require(!invalid_request_header &&
              invalid_request_header.status() == STATUS_DATA_ERROR,
          "HTTP/2 normalized an uppercase request header name");

  const std::vector<field> valid_request{
      {":method", "GET", false},
      {":scheme", "https", false},
      {":authority", "one.example.test", false},
      {":path", "/", false}};
  const std::vector<field> uppercase_trailer{
      {"X-Trailer", "value", false}};
  const auto invalid_request_trailer = ntl::net::http2::parse_request(
      valid_request, {}, uppercase_trailer, decoders);
  require(!invalid_request_trailer &&
              invalid_request_trailer.status() == STATUS_DATA_ERROR,
          "HTTP/2 normalized an uppercase request trailer name");

  const std::vector<field> switching_protocols{{":status", "101", false}};
  const auto invalid_upgrade = ntl::net::http2::parse_response(
      switching_protocols, {}, {}, decoders);
  require(!invalid_upgrade &&
              invalid_upgrade.status() == STATUS_DATA_ERROR,
          "HTTP/2 accepted the HTTP/1.1 101 Upgrade response");

  const std::vector<field> uppercase_response_header{
      {":status", "200", false}, {"X-Policy", "inspect", false}};
  const auto invalid_response_header = ntl::net::http2::parse_response(
      uppercase_response_header, {}, {}, decoders);
  require(!invalid_response_header &&
              invalid_response_header.status() == STATUS_DATA_ERROR,
          "HTTP/2 normalized an uppercase response header name");
  const std::vector<field> valid_response{{":status", "200", false}};
  const auto invalid_response_trailer = ntl::net::http2::parse_response(
      valid_response, {}, uppercase_trailer, decoders);
  require(!invalid_response_trailer &&
              invalid_response_trailer.status() == STATUS_DATA_ERROR,
          "HTTP/2 normalized an uppercase response trailer name");

  ntl::net::http2::exchange_store retained(2, 32);
  ntl::net::http::request_message small;
  small.wire_protocol = ntl::net::http::protocol::http2;
  small.method = "GET";
  small.scheme = "https";
  small.authority = "a";
  small.path = "/";
  require(retained.remember(1, small).is_ok(),
          "bounded request retention rejected a small request");
  const auto first = retained.shared_request(1);
  const auto second = retained.shared_request(1);
  require(first && second && first->get() == second->get(),
          "response association copied a retained request");

  auto aggregate = small;
  aggregate.body.resize(18, std::byte{'x'});
  require(retained.remember(3, aggregate) == STATUS_QUOTA_EXCEEDED,
          "aggregate retained-request quota was not enforced");
  retained.erase(1);
  require(retained.remember(3, std::move(aggregate)).is_ok(),
          "retained-request bytes were not released with the exchange");
}

ntl::status strict_authority_case(
    std::optional<std::string> server_name,
    std::string authority) {
  auto decoders = std::make_shared<
      ntl::net::inspection::content_decoder_registry>();
  auto encoders = std::make_shared<
      ntl::net::inspection::content_encoder_registry>();
  auto pipeline = std::make_shared<ntl::net::http::transform_pipeline>();
  ntl::net::http::inspection_session_metadata metadata;
  metadata.tls.server_name = std::move(server_name);
  metadata.tls.alpn = "h2";
  ntl::net::http2::proxy_connection connection(
      pipeline, decoders, encoders, std::move(metadata),
      ntl::net::http2::inspection_observer{},
      {.require_server_name_authority_binding = true});
  require(connection.accept_client_preface(
              ntl::net::http2::client_connection_preface)
              .is_ok(),
          "authority fixture rejected the client preface");
  const auto client_settings = settings();
  const auto server_settings = settings();
  require(connection.consume(connection_direction::requests,
                             parse(client_settings)) &&
              connection.consume(connection_direction::responses,
                                 parse(server_settings)),
          "authority fixture rejected SETTINGS");

  auto message = request("/");
  message.authority = std::move(authority);
  const auto frames = ntl::net::http2::encode_request_frames(
      1, message, {}, 16, 16 * 1024, true);
  require(frames.has_value(), "authority request encoding failed");
  for (const auto &wire : *frames) {
    const auto step = connection.consume(
        connection_direction::requests, parse(wire));
    if (!step)
      return step.status();
    if (step->message_complete)
      return step->request && !step->forward.empty()
                 ? ntl::status::ok()
                 : ntl::status(STATUS_DATA_ERROR);
  }
  return STATUS_DATA_ERROR;
}

void test_strict_sni_authority_binding() {
  require(strict_authority_case(
              std::string("Policy.Example.Test"),
              "policy.example.test").is_ok(),
          "case-insensitive SNI authority match failed");
  require(strict_authority_case(
              std::string("policy.example.test."),
              "POLICY.EXAMPLE.TEST:443").is_ok(),
          "trailing-dot/default-port SNI authority match failed");
  require(strict_authority_case(
              std::nullopt, "policy.example.test") == STATUS_ACCESS_DENIED,
          "missing SNI did not fail closed");
  require(strict_authority_case(
              std::string("other.example.test"),
              "policy.example.test") == STATUS_ACCESS_DENIED,
          "mismatched SNI and authority were accepted");
  require(strict_authority_case(
              std::string("policy.example.test"),
              "policy.example.test:444") == STATUS_ACCESS_DENIED,
          "non-origin authority port was accepted");
}

void test_connect_admission_reset_goaway_and_bounds() {
  ntl::net::http::request_message ordinary_connect;
  ordinary_connect.wire_protocol = ntl::net::http::protocol::http2;
  ordinary_connect.method = "CONNECT";
  ordinary_connect.scheme.clear();
  ordinary_connect.authority = "policy.example.test:443";
  ordinary_connect.path.clear();

  fixture ordinary_state;
  auto ordinary_open =
      feed_request(ordinary_state, 5, ordinary_connect, {}, false);
  require(ordinary_open.request.has_value() && !ordinary_open.forward.empty(),
          "ordinary CONNECT was not presented for pre-forward admission");
  ntl::net::http2::reject_tunnel_handler reject_by_default;
  const auto ordinary_disposition =
      reject_by_default.admit(5, *ordinary_open.request);
  require(ordinary_disposition &&
              *ordinary_disposition ==
                  ntl::net::http2::connect_disposition::reject &&
              ordinary_state.connection
                  .admit_connect(ordinary_open, *ordinary_disposition)
                  .is_ok() &&
              ordinary_open.terminal_status == 403 &&
              ordinary_open.forward.empty() &&
              !ordinary_open.reverse.empty() &&
              !ordinary_state.connection.is_admitted_connect(5),
          "ordinary CONNECT reached the origin-facing output by default");

  fixture ordinary_passthrough_state;
  auto ordinary_passthrough_open = feed_request(
      ordinary_passthrough_state, 13, ordinary_connect, {}, false);
  ntl::net::http2::passthrough_tunnel_handler byte_tunnel;
  const auto ordinary_passthrough =
      byte_tunnel.admit(13, *ordinary_passthrough_open.request);
  require(ordinary_passthrough &&
              *ordinary_passthrough ==
                  ntl::net::http2::connect_disposition::passthrough &&
              ordinary_passthrough_state.connection
                  .admit_connect(
                      ordinary_passthrough_open, *ordinary_passthrough)
                  .is_ok() &&
              !ordinary_passthrough_open.forward.empty() &&
              ordinary_passthrough_state.connection
                  .is_admitted_connect(13),
          "explicit ordinary CONNECT byte-tunnel admission failed");
  ntl::net::http::response_message ordinary_accepted;
  ordinary_accepted.wire_protocol = ntl::net::http::protocol::http2;
  ordinary_accepted.status = 200;
  const auto ordinary_established = feed_response(
      ordinary_passthrough_state, 13, ordinary_accepted, {}, true, false);
  require(ordinary_established.response &&
              ordinary_passthrough_state.connection
                  .is_established_tunnel(13),
          "explicit ordinary CONNECT byte tunnel was not established");

  ntl::net::http::request_message connect;
  connect.wire_protocol = ntl::net::http::protocol::http2;
  connect.method = "CONNECT";
  connect.scheme = "https";
  connect.authority = "policy.example.test";
  connect.path = "/chat";
  connect.extended_protocol = "websocket";

  fixture rejected_state;
  auto rejected_open = feed_request(rejected_state, 7, connect, {}, false);
  require(rejected_open.request.has_value(),
          "Extended CONNECT request was not retained for admission");
  const auto rejected_disposition =
      reject_by_default.admit(7, *rejected_open.request);
  require(rejected_disposition &&
              *rejected_disposition ==
                  ntl::net::http2::connect_disposition::reject &&
              rejected_state.connection
                  .admit_connect(
                      rejected_open, *rejected_disposition)
                  .is_ok() &&
              rejected_open.terminal_status == 403 &&
              rejected_open.forward.empty() &&
              !rejected_open.reverse.empty() &&
              !rejected_state.connection.is_admitted_connect(7),
          "default Extended CONNECT admission was not fail-closed");

  fixture passthrough_state;
  auto passthrough_open =
      feed_request(passthrough_state, 11, connect, {}, false);
  require(passthrough_open.request.has_value(),
          "passthrough CONNECT request was not retained for admission");
  ntl::net::http2::passthrough_tunnel_handler explicit_passthrough;
  const auto passthrough_disposition =
      explicit_passthrough.admit(11, *passthrough_open.request);
  require(passthrough_disposition &&
              *passthrough_disposition ==
                  ntl::net::http2::connect_disposition::passthrough &&
              passthrough_state.connection
                  .admit_connect(
                      passthrough_open, *passthrough_disposition)
                  .is_ok() &&
              passthrough_state.connection.connect_mode(11) ==
                  ntl::net::http2::connect_disposition::passthrough,
          "explicit Extended CONNECT passthrough was not recorded");

  fixture state;
  const auto opened = feed_request(state, 9, connect, {}, false);
  auto inspected_open = opened;
  require(inspected_open.request &&
              !state.connection.is_admitted_connect(9) &&
              state.connection
                  .admit_connect(
                      inspected_open,
                      ntl::net::http2::connect_disposition::inspect)
                  .is_ok() &&
              state.connection.is_admitted_connect(9) &&
              state.connection.connect_mode(9) ==
                  ntl::net::http2::connect_disposition::inspect,
          "explicit Extended CONNECT inspection admission failed");
  ntl::net::http::response_message accepted;
  accepted.wire_protocol = ntl::net::http::protocol::http2;
  accepted.status = 200;
  const auto established =
      feed_response(state, 9, accepted, {}, true, false);
  require(established.response &&
              state.connection.is_established_tunnel(9),
          "Extended CONNECT tunnel was not established");
  auto tunnel_data = ntl::net::http2::transform_detail::make_frame(
      ntl::net::http2::frame_type::data, 0, 9, bytes("websocket-wire"));
  require(tunnel_data.has_value(), "tunnel DATA encoding failed");
  auto tunnel_step = state.connection.consume(
      connection_direction::requests, parse(*tunnel_data));
  require(tunnel_step && tunnel_step->forward_original &&
              !tunnel_step->consumed,
          "established tunnel DATA did not retain frame carryover");
  auto credits = state.connection.acknowledge_forwarded_data(
      9, tunnel_data->flow_controlled_bytes, false);
  require(credits && credits->size() == 2,
          "opaque tunnel credit was not deferred until forwarding");

  auto reset = control_frame(
      ntl::net::http2::frame_type::reset_stream, 9,
      {std::byte{0}, std::byte{0}, std::byte{0}, std::byte{8}});
  auto reset_step = state.connection.consume(
      connection_direction::requests, parse(reset));
  require(reset_step && reset_step->forward_original,
          "RST_STREAM was not forwarded after local cleanup");

  const auto shutdown = goaway(9, 0);
  auto goaway_step = state.connection.consume(
      connection_direction::responses, parse(shutdown));
  require(goaway_step && goaway_step->draining &&
              state.connection.upstream_goaway().last_stream_id == 9,
          "GOAWAY drain state was not recorded");
  auto too_late = request("/after-goaway");
  auto late_frames = ntl::net::http2::encode_request_frames(11, too_late, {});
  require(late_frames.has_value(), "late request encoding failed");
  const auto rejected = state.connection.consume(
      connection_direction::requests, parse(late_frames->front()));
  require(!rejected &&
              rejected.status() == STATUS_CONNECTION_DISCONNECTED,
          "stream beyond GOAWAY last-stream-id was not rejected");

  fixture bounded(4);
  auto oversized = request("/bounded", "POST");
  const auto five = bytes("12345");
  oversized.headers.append("content-length", "5");
  auto encoded = ntl::net::http2::encode_request_frames(1, oversized, five);
  require(encoded.has_value(), "oversized fixture encoding failed");
  bool failed_closed = false;
  for (const auto &wire : *encoded) {
    auto step = bounded.connection.consume(
        connection_direction::requests, parse(wire));
    if (!step) {
      failed_closed = step.status() == STATUS_BUFFER_OVERFLOW;
      break;
    }
  }
  require(failed_closed, "bounded message overflow did not fail closed");
}

void test_preface_and_first_settings_contract() {
  auto decoders = std::make_shared<
      ntl::net::inspection::content_decoder_registry>();
  auto encoders = std::make_shared<
      ntl::net::inspection::content_encoder_registry>();
  auto pipeline = std::make_shared<ntl::net::http::transform_pipeline>();
  ntl::net::http2::proxy_connection connection(
      pipeline, decoders, encoders);
  std::array<std::byte, 24> invalid{};
  require(!connection.accept_client_preface(invalid).is_ok(),
          "invalid client connection preface was accepted");

  ntl::net::http2::proxy_connection second(pipeline, decoders, encoders);
  require(second.accept_client_preface(
                   ntl::net::http2::client_connection_preface)
              .is_ok(),
          "valid client connection preface was rejected");
  auto ping = ntl::net::http2::transform_detail::make_frame(
      ntl::net::http2::frame_type::ping, 0, 0,
      std::array<std::byte, 8>{});
  require(ping.has_value(), "PING encoding failed");
  const auto rejected = second.consume(
      connection_direction::requests, parse(*ping));
  require(!rejected && rejected.status() == STATUS_DATA_ERROR,
          "first non-SETTINGS frame was accepted");
}

void test_close_from_callback_and_owner_first() {
  auto decoders = std::make_shared<
      ntl::net::inspection::content_decoder_registry>();
  auto encoders = std::make_shared<
      ntl::net::inspection::content_encoder_registry>();
  auto policy = std::make_shared<ntl::net::http::inspection_policy>();
  policy->use_content_codecs(decoders, encoders);
  auto observer = std::make_shared<close_on_observation>();
  auto connection = std::make_shared<ntl::net::http2::proxy_connection>(
      policy, ntl::net::http::inspection_session_metadata{},
      ntl::net::http2::inspection_observer(observer));
  observer->owner = connection;

  require(connection->accept_client_preface(
              ntl::net::http2::client_connection_preface)
              .is_ok(),
          "callback-close preface was rejected");
  const auto client_settings = settings();
  const auto server_settings = settings();
  require(static_cast<bool>(connection->consume(
              connection_direction::requests, parse(client_settings))),
          "callback-close client SETTINGS was rejected");
  require(static_cast<bool>(connection->consume(
              connection_direction::responses, parse(server_settings))),
          "callback-close server SETTINGS was rejected");

  auto frames = ntl::net::http2::encode_request_frames(
      1, request("/close-from-callback"), {});
  require(frames.has_value(), "callback-close request encoding failed");
  ntl::status terminal = ntl::status::ok();
  for (const auto &wire : *frames) {
    auto step = connection->consume(
        connection_direction::requests, parse(wire));
    if (!step) {
      terminal = step.status();
      break;
    }
  }
  require(connection->is_closed() && terminal == STATUS_DELETE_PENDING,
          "HTTP/2 callback close did not reject remaining work");
  connection->close();
  connection->close();
  require(connection->accept_client_preface(
              ntl::net::http2::client_connection_preface) ==
              STATUS_DELETE_PENDING,
          "closed HTTP/2 proxy accepted a new preface");

  std::weak_ptr<ntl::net::http2::proxy_connection> lifetime = connection;
  connection.reset();
  require(lifetime.expired(),
          "HTTP/2 callback observer retained its owner");
}

void test_peer_maximum_frame_size_is_an_outbound_bound() {
  const auto verify_payload_bound = [](const auto &frames,
                                       std::size_t maximum,
                                       bool require_larger_than_default) {
    bool larger_than_default = false;
    for (const auto &frame : frames) {
      const auto parsed = parse(frame);
      require(parsed.header().payload_size <= maximum,
              "transformed frame exceeded peer SETTINGS_MAX_FRAME_SIZE");
      larger_than_default = larger_than_default ||
          parsed.header().payload_size >
              ntl::net::http2::default_maximum_frame_size;
    }
    require(!require_larger_than_default || larger_than_default,
            "updated peer frame limit was not applied to transformed output");
  };

  const auto body = std::vector<std::byte>(40 * 1024, std::byte{'x'});
  ntl::net::http::response_message response;
  response.wire_protocol = ntl::net::http::protocol::http2;
  response.status = 200;
  response.headers.append("content-type", "application/octet-stream");
  response.headers.append("content-length", std::to_string(body.size()));

  fixture defaults(64 * 1024);
  (void)feed_request(defaults, 1, request("/default-frame-limit"));
  const auto default_result =
      feed_response(defaults, 1, response, body);
  verify_payload_bound(default_result.forward,
                       ntl::net::http2::default_maximum_frame_size, false);
  require(defaults.connection.destination_maximum_frame_payload(
              connection_direction::responses) ==
              ntl::net::http2::default_maximum_frame_size,
          "HTTP/2 default peer frame size was not 16,384");

  fixture enlarged(64 * 1024);
  const auto client_update = settings(false,
      ntl::net::http2::send_window::default_window, 32 * 1024);
  require(enlarged.connection.consume(connection_direction::requests,
                                      parse(client_update))
              .has_value(),
          "valid client SETTINGS_MAX_FRAME_SIZE was rejected");
  require(enlarged.connection.destination_maximum_frame_payload(
              connection_direction::responses) == 32 * 1024,
          "client SETTINGS_MAX_FRAME_SIZE did not update response output");
  (void)feed_request(enlarged, 3, request("/enlarged-frame-limit"));
  const auto enlarged_result =
      feed_response(enlarged, 3, response, body);
  verify_payload_bound(enlarged_result.forward, 32 * 1024, true);
}

void test_local_preflight_settings_ack_is_not_replayed() {
  fixture state;
  require(state.connection.expect_local_downstream_settings_ack().is_ok(),
          "local preflight SETTINGS was not recorded");
  require(state.connection.expect_local_downstream_settings_ack() ==
              STATUS_INVALID_DEVICE_STATE,
          "two unacknowledged local SETTINGS frames were accepted");

  const auto acknowledgement = settings_acknowledgement();
  auto local_ack = state.connection.consume(
      connection_direction::requests, parse(acknowledgement));
  require(local_ack.has_value() && local_ack->consumed &&
              !local_ack->forward_original && local_ack->forward.empty(),
          "local preflight SETTINGS acknowledgement was replayed upstream");

  auto origin_ack = state.connection.consume(
      connection_direction::requests, parse(acknowledgement));
  require(origin_ack.has_value() && origin_ack->forward_original,
          "origin SETTINGS acknowledgement was suppressed");
}

void test_internal_waiters_unregister_when_frames_are_abandoned() {
  using ntl::net::http2::proxy_session_detail::async_barrier;
  using ntl::net::http2::proxy_session_detail::async_mutex;

  async_mutex mutex;
  auto first = mutex.acquire();
  require(first.await_ready(), "HTTP/2 mutex did not grant initial owner");
  auto ownership = first.await_resume();
  {
    auto abandoned = mutex.acquire();
    require(!abandoned.await_ready(),
            "HTTP/2 mutex unexpectedly granted a second owner");
    require(abandoned.await_suspend(std::noop_coroutine()),
            "HTTP/2 mutex did not queue the abandoned waiter");
  }
  ownership = {};
  auto next = mutex.acquire();
  require(next.await_ready(),
          "abandoned HTTP/2 mutex waiter remained registered");
  auto next_ownership = next.await_resume();

  async_barrier barrier;
  {
    auto abandoned = barrier.wait();
    require(!abandoned.await_ready(),
            "HTTP/2 barrier was ready before it was signaled");
    require(abandoned.await_suspend(std::noop_coroutine()),
            "HTTP/2 barrier did not queue the abandoned waiter");
  }
  barrier.signal();
  auto completed = barrier.wait();
  require(completed.await_ready(),
          "HTTP/2 barrier lost its signal after waiter abandonment");
  completed.await_resume();
}

} // namespace

int main() {
  try {
    test_preface_and_first_settings_contract();
    test_close_from_callback_and_owner_first();
    test_context_continuation_flow_and_response_association();
    test_header_block_body_drop_and_head();
    test_terminal_transform_and_empty_body_stages();
    test_pseudo_headers_authority_and_request_retention();
    test_strict_sni_authority_binding();
    test_connect_admission_reset_goaway_and_bounds();
    test_peer_maximum_frame_size_is_an_outbound_bound();
    test_local_preflight_settings_ack_is_not_replayed();
    test_internal_waiters_unregister_when_frames_are_abandoned();
    std::cout << "HTTP/2 proxy connection contracts passed\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
