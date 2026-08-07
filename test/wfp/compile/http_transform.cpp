#include <atomic>
#include <chrono>
#include <coroutine>
#include <cstddef>
#include <future>
#include <iostream>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include <ntl/net/http/http1_transform>
#include <ntl/net/borrowed_memory_resource>
#include <ntl/net/http/http1_stream_transform>
#include <ntl/net/http/async_transform>
#include <ntl/net/http/inspection_resource_profile>
#include <ntl/net/http/stream_transform>
#include <ntl/net/http/transform>
#include <ntl/net/http2/transform>
#include <ntl/net/http2/flow_control>
#include <ntl/net/http2/proxy_connection>
#include <ntl/net/http2/stream_transform>
#include <ntl/net/http3/stream_transform>
#include <ntl/net/inspection/standard_content_decoders>
#include <ntl/net/inspection/standard_content_encoders>

namespace {

static_assert(ntl::net::http::inspection_resource_profile{}.valid());
static_assert([] {
  auto profile = ntl::net::http::inspection_resource_profile{};
  profile.http1_workspace_budget =
      profile.http1.maximum_wire_message_size - 1;
  return profile.validate() == ntl::net::http::inspection_resource_error::
                                   http1_workspace_budget_too_small;
}());

template <class T> class blocking_task {
public:
  struct promise_type {
    blocking_task get_return_object() {
      return blocking_task(result.get_future());
    }
    std::suspend_never initial_suspend() const noexcept { return {}; }
    std::suspend_never final_suspend() const noexcept { return {}; }
    template <class U> void return_value(U &&value) {
      result.set_value(std::forward<U>(value));
    }
    void unhandled_exception() noexcept {
      result.set_exception(std::current_exception());
    }
    std::promise<T> result;
  };

  blocking_task(blocking_task &&) noexcept = default;
  blocking_task &operator=(blocking_task &&) noexcept = default;
  blocking_task(const blocking_task &) = delete;
  blocking_task &operator=(const blocking_task &) = delete;

  T get() { return result_.get(); }

private:
  explicit blocking_task(std::future<T> result) noexcept
      : result_(std::move(result)) {}
  std::future<T> result_;
};

blocking_task<ntl::net::http::pipeline_outcome> apply_async(
    ntl::net::http::async_transform_runtime &pipeline,
    ntl::net::http::request_message &message,
    std::stop_token cancellation = {}) {
  co_return co_await pipeline.apply_borrowed(message, cancellation);
}

blocking_task<ntl::net::http::async_transform_result<
    ntl::net::http::request_message>>
apply_async_owned(
    ntl::net::http::async_transform_runtime &pipeline,
    ntl::net::http::request_message message) {
  co_return co_await pipeline.apply(std::move(message));
}

blocking_task<bool> wait_for_send_window_owner_close(
    ntl::net::http2::send_window &window) {
  try {
    co_await window.reserve(1, 1);
    co_return false;
  } catch (const std::runtime_error &) {
    co_return true;
  }
}

std::vector<std::byte> bytes(std::string_view value) {
  std::vector<std::byte> result(value.size());
  for (std::size_t index = 0; index != value.size(); ++index)
    result[index] = static_cast<std::byte>(
        static_cast<unsigned char>(value[index]));
  return result;
}

std::string text(std::span<const std::byte> value) {
  std::string result(value.size(), '\0');
  for (std::size_t index = 0; index != value.size(); ++index)
    result[index] = static_cast<char>(
        std::to_integer<unsigned char>(value[index]));
  return result;
}

ntl::net::http::request_message request() {
  ntl::net::http::request_message result;
  result.wire_protocol = ntl::net::http::protocol::http2;
  result.method = "GET";
  result.scheme = "https";
  result.authority = "example.test";
  result.path = "/";
  result.headers.append("accept", "text/html");
  return result;
}

ntl::net::http::response_message response() {
  ntl::net::http::response_message result;
  result.wire_protocol = ntl::net::http::protocol::http2;
  result.status = 200;
  result.headers.append("content-type", "text/html; charset=utf-8");
  result.headers.append("etag", "\"stale\"");
  result.headers.append("content-length", "28");
  result.body = bytes("<html><body>x</body></html>");
  return result;
}

bool test_pipeline() {
  ntl::net::http::transform_pipeline pipeline;
  pipeline.requests().transform(
      [](ntl::net::http::request_message &message) {
        message.headers.set("x-ntl-inspected", "true");
        return ntl::net::http::rewrite_result::headers_changed();
      });
  pipeline.responses()
      .html()
      .inspect(
          [](const ntl::net::http::request_message &,
             const ntl::net::http::response_message &) {
            return ntl::net::inspection::verdict::permit;
          })
      .transform(
          [](const ntl::net::http::request_message &,
             ntl::net::http::response_message &message) {
            std::string html = text(message.body);
            const auto tail = html.find("</body>");
            if (tail == std::string::npos)
              return ntl::net::http::rewrite_result::block();
            html.insert(tail, "<b>ntl</b>");
            return ntl::net::http::rewrite_result::replace_text(
                std::move(html));
          });

  auto incoming = request();
  const auto request_result = pipeline.apply(incoming);
  if (request_result.action !=
          ntl::net::http::rewrite_action::forward ||
      !request_result.headers_modified ||
      incoming.headers.first("x-ntl-inspected") != "true")
    return false;

  auto outgoing = response();
  const auto response_result =
      pipeline.apply(incoming, outgoing);
  return response_result.action ==
             ntl::net::http::rewrite_action::forward &&
         response_result.body_modified &&
         text(outgoing.body).find("<b>ntl</b>") !=
             std::string::npos &&
         !outgoing.headers.contains("etag") &&
         outgoing.headers.first("content-length") ==
             std::to_string(outgoing.body.size());
}

bool test_content_reencoding() {
  ntl::net::inspection::content_decoder_registry decoders;
  ntl::net::inspection::content_encoder_registry encoders;
  ntl::net::inspection::register_standard_content_decoders(decoders);
  ntl::net::inspection::register_standard_content_encoders(encoders);

  const auto plain = bytes(
      "<!doctype html><html><body>compressed rewrite</body></html>");
  ntl::net::http::header_collection headers;
  headers.append("content-encoding", "gzip, br");
  auto encoded = ntl::net::http::encode_body(
      headers, plain, "gzip, br",
      ntl::net::http::transformed_body_coding::preserve,
      encoders);
  if (!encoded || *encoded == plain ||
      headers.first("content-encoding") != "gzip, br")
    return false;

  auto decoded =
      ntl::net::http::decode_body(headers, *encoded, decoders);
  return decoded && decoded->bytes == plain &&
         decoded->original_content_encoding == "gzip, br";
}

bool test_terminal_results() {
  ntl::net::http::transform_pipeline blocked;
  blocked.requests().decide(
      [](const ntl::net::http::request_message &) {
        return ntl::net::inspection::verdict::block;
      });
  auto first = request();
  if (blocked.apply(first).action !=
      ntl::net::http::rewrite_action::block)
    return false;

  ntl::net::http::transform_pipeline synthetic;
  synthetic.requests().transform(
      [](ntl::net::http::request_message &) {
        ntl::net::http::response_message response;
        response.wire_protocol =
            ntl::net::http::protocol::http2;
        response.status = 403;
        response.headers.append(
            "content-type", "text/plain");
        response.body = bytes("blocked");
        response.headers.append("content-length", "7");
        return ntl::net::http::rewrite_result::respond(
            std::move(response));
      });
  auto second = request();
  const auto result = synthetic.apply(second);
  return result.action ==
             ntl::net::http::rewrite_action::respond &&
         result.response && result.response->status == 403;
}

bool test_failure_policy() {
  ntl::net::http::transform_pipeline fail_closed;
  fail_closed.responses().transform(
      [](const ntl::net::http::request_message &,
         ntl::net::http::response_message &) ->
          ntl::net::http::rewrite_result {
        throw std::runtime_error("policy failure");
      });
  auto blocked = response();
  if (fail_closed.apply(request(), blocked).action !=
      ntl::net::http::rewrite_action::block)
    return false;

  ntl::net::http::transform_limits limits;
  limits.on_failure =
      ntl::net::http::transform_failure_policy::
          forward_original;
  ntl::net::http::transform_pipeline fail_open(limits);
  fail_open.responses().transform(
      [](const ntl::net::http::request_message &,
         ntl::net::http::response_message &message) ->
          ntl::net::http::rewrite_result {
        message.status = 500;
        message.body = bytes("partially changed");
        throw std::runtime_error("policy failure");
      });
  auto original = response();
  const auto expected = original;
  const auto outcome = fail_open.apply(request(), original);
  return outcome.action ==
             ntl::net::http::rewrite_action::forward &&
         outcome.failure != STATUS_SUCCESS &&
         !outcome.modified() &&
         original.status == expected.status &&
         original.body == expected.body &&
         original.headers.first("etag") ==
             expected.headers.first("etag");
}

bool test_protocol_safety_and_bodyless_responses() {
  ntl::net::http::transform_pipeline validation;
  auto invalid_authority = request();
  invalid_authority.authority = "example.test\r\nx-injected: yes";
  if (validation.apply(invalid_authority).action !=
      ntl::net::http::rewrite_action::block)
    return false;

  auto invalid_trailer = request();
  invalid_trailer.trailers.push_back(
      {"content-length", "1", false});
  if (validation.apply(invalid_trailer).action !=
      ntl::net::http::rewrite_action::block)
    return false;

  ntl::net::http::transform_pipeline rewrite;
  rewrite.responses().transform(
      [](const ntl::net::http::request_message &,
         ntl::net::http::response_message &message) {
        message.headers.set("x-ntl-bodyless", "true");
        return ntl::net::http::rewrite_result::
            headers_changed();
      });

  ntl::net::inspection::content_decoder_registry decoders;
  ntl::net::inspection::content_encoder_registry encoders;
  ntl::net::inspection::register_standard_content_decoders(decoders);
  ntl::net::inspection::register_standard_content_encoders(encoders);

  auto head_request = request();
  head_request.wire_protocol =
      ntl::net::http::protocol::http1;
  head_request.method = "HEAD";
  const auto head_wire = bytes(
      "HTTP/1.1 200 OK\r\n"
      "Content-Encoding: gzip\r\n"
      "Content-Length: 123\r\n\r\n");
  auto http1 = ntl::net::http::transform_http1_response(
      head_wire, head_request, rewrite, decoders, encoders);
  if (!http1 ||
      http1->message.headers.first("content-length") != "123" ||
      http1->message.headers.first("x-ntl-bodyless") != "true" ||
      !http1->message.body.empty() ||
      !text(http1->wire).ends_with("\r\n\r\n"))
    return false;

  head_request.wire_protocol =
      ntl::net::http::protocol::http2;
  const std::vector<ntl::net::http2::header_field> fields{
      {":status", "200", false},
      {"content-encoding", "gzip", false},
      {"content-length", "123", false}};
  auto http2 = ntl::net::http2::transform_response(
      9, head_request, fields, {}, {}, rewrite,
      decoders, encoders);
  if (!http2 || http2->frames.empty() ||
      http2->message.headers.first("content-length") != "123" ||
      !http2->message.body.empty())
    return false;
  for (const auto &encoded : http2->frames) {
    auto frame = ntl::net::http2::frame_view::parse(
        ntl::net::scatter_view::from_contiguous(encoded.wire),
        {64 * 1024, false});
    if (!frame ||
        frame->header().type ==
            ntl::net::http2::frame_type::data)
      return false;
  }

  ntl::net::http::transform_pipeline invalid_synthetic;
  invalid_synthetic.requests().transform(
      [](ntl::net::http::request_message &) {
        ntl::net::http::response_message response;
        response.status = 403;
        response.body = bytes("must not be sent for HEAD");
        return ntl::net::http::rewrite_result::respond(
            std::move(response));
      });
  auto synthetic_head = head_request;
  const auto synthetic_result =
      invalid_synthetic.apply(synthetic_head);
  return synthetic_result.action ==
             ntl::net::http::rewrite_action::block &&
         synthetic_result.failure != STATUS_SUCCESS;
}

bool test_http1_backend() {
  ntl::net::inspection::content_decoder_registry decoders;
  ntl::net::inspection::content_encoder_registry encoders;
  ntl::net::inspection::register_standard_content_decoders(decoders);
  ntl::net::inspection::register_standard_content_encoders(encoders);

  ntl::net::http::transform_pipeline pipeline;
  pipeline.requests().transform(
      [](ntl::net::http::request_message &message) {
        message.headers.erase("proxy-connection");
        message.headers.set("accept-encoding", "identity");
        return ntl::net::http::rewrite_result::headers_changed();
      });
  pipeline.responses().html().transform(
      [](const ntl::net::http::request_message &,
         ntl::net::http::response_message &message) {
        std::string html = text(message.body);
        const auto tail = html.find("</body>");
        if (tail == std::string::npos)
          return ntl::net::http::rewrite_result::block();
        html.insert(tail, "<i>rewritten</i>");
        return ntl::net::http::rewrite_result::replace_text(
            std::move(html),
            ntl::net::http::transformed_body_coding::identity);
      });

  const auto request_wire = bytes(
      "GET /index HTTP/1.1\r\n"
      "Host: example.test\r\n"
      "Accept-Encoding: gzip\r\n"
      "Proxy-Connection: keep-alive\r\n"
      "Content-Length: 0\r\n\r\n");
  auto transformed_request =
      ntl::net::http::transform_http1_request(
          request_wire, pipeline, decoders, encoders,
          {.origin_scheme = "https"});
  if (!transformed_request ||
      transformed_request->message.authority != "example.test" ||
      text(transformed_request->wire).find(
          "accept-encoding: identity\r\n") ==
          std::string::npos ||
      text(transformed_request->wire).find("proxy-connection") !=
          std::string::npos)
    return false;

  const auto response_wire = bytes(
      "HTTP/1.1 200 OK\r\n"
      "Content-Type: text/html\r\n"
      "Transfer-Encoding: chunked\r\n"
      "ETag: \"old\"\r\n\r\n"
      "1b\r\n<html><body>x</body></html>\r\n"
      "0\r\n\r\n");
  auto transformed_response =
      ntl::net::http::transform_http1_response(
          response_wire, transformed_request->message,
          pipeline, decoders, encoders);
  if (!transformed_response ||
      !transformed_response->outcome.body_modified)
    return false;
  const std::string response_text =
      text(transformed_response->wire);
  return response_text.find("<i>rewritten</i>") !=
             std::string::npos &&
         response_text.find("transfer-encoding") ==
             std::string::npos &&
         response_text.find("etag") == std::string::npos &&
         response_text.find("content-length:") !=
             std::string::npos;
}

bool test_http2_backend() {
  ntl::net::inspection::content_decoder_registry decoders;
  ntl::net::inspection::content_encoder_registry encoders;
  ntl::net::inspection::register_standard_content_decoders(decoders);
  ntl::net::inspection::register_standard_content_encoders(encoders);

  ntl::net::http::transform_pipeline pipeline;
  pipeline.requests().transform(
      [](ntl::net::http::request_message &message) {
        message.headers.set("x-ntl-inspected", "h2");
        return ntl::net::http::rewrite_result::headers_changed();
      });
  pipeline.responses().html().transform(
      [](const ntl::net::http::request_message &,
         ntl::net::http::response_message &message) {
        std::string html = text(message.body);
        html.append("<!-- h2 -->");
        return ntl::net::http::rewrite_result::replace_text(
            std::move(html));
      });

  const std::vector<ntl::net::http2::header_field>
      request_fields{
          {":method", "GET", false},
          {":scheme", "https", false},
          {":authority", "example.test", false},
          {":path", "/h2", false},
          {"accept", "text/html", false}};
  auto transformed_request =
      ntl::net::http2::transform_request(
          1, request_fields, {}, {}, pipeline,
          decoders, encoders, 16);
  if (!transformed_request ||
      transformed_request->frames.empty() ||
      transformed_request->message.headers.first(
          "x-ntl-inspected") != "h2")
    return false;

  ntl::net::http2::header_block_assembler request_headers;
  ntl::net::http2::bounded_hpack_decoder request_decoder;
  std::optional<ntl::net::http2::decoded_headers>
      decoded_request_headers;
  for (const auto &encoded : transformed_request->frames) {
    auto frame = ntl::net::http2::frame_view::parse(
        ntl::net::scatter_view::from_contiguous(encoded.wire),
        {64, false});
    if (!frame)
      return false;
    if (frame->header().type ==
            ntl::net::http2::frame_type::headers ||
        frame->header().type ==
            ntl::net::http2::frame_type::continuation) {
      auto block = request_headers.consume(*frame);
      if (!block)
        return false;
      if (*block) {
        auto decoded = request_decoder.decode(
            ntl::net::scatter_view::from_contiguous(
                (**block).encoded),
            64 * 1024);
        if (!decoded)
          return false;
        decoded_request_headers = std::move(*decoded);
      }
    }
  }
  bool marker_seen = false;
  if (!decoded_request_headers)
    return false;
  for (const auto &field : decoded_request_headers->fields)
    marker_seen = marker_seen ||
                  (field.name == "x-ntl-inspected" &&
                   field.value == "h2");
  if (!marker_seen)
    return false;

  const auto html = bytes("<html><body>h2</body></html>");
  const std::vector<ntl::net::http2::header_field>
      response_fields{
          {":status", "200", false},
          {"content-type", "text/html", false},
          {"content-length", std::to_string(html.size()), false},
          {"etag", "\"stale\"", false}};
  auto transformed_response =
      ntl::net::http2::transform_response(
          1, transformed_request->message,
          response_fields, html, {}, pipeline,
          decoders, encoders, 8);
  if (!transformed_response ||
      !transformed_response->outcome.body_modified ||
      transformed_response->message.headers.contains("etag") ||
      text(transformed_response->message.body).find(
          "<!-- h2 -->") == std::string::npos)
    return false;

  std::vector<std::byte> forwarded_body;
  bool end_stream = false;
  for (const auto &encoded : transformed_response->frames) {
    auto frame = ntl::net::http2::frame_view::parse(
        ntl::net::scatter_view::from_contiguous(encoded.wire),
        {64, false});
    if (!frame)
      return false;
    if (frame->header().type ==
        ntl::net::http2::frame_type::data) {
      auto data = frame->data_payload();
      if (!data)
        return false;
      const std::size_t offset = forwarded_body.size();
      forwarded_body.resize(offset + data->size());
      if (!data->copy_to(
                    std::span<std::byte>(forwarded_body)
                        .subspan(offset))
               .is_ok())
        return false;
      end_stream = frame->header().end_stream();
    }
  }
  return end_stream &&
         forwarded_body ==
             transformed_response->message.body;
}

bool test_http2_connection_transformer() {
  const auto failed = [](int step) {
    std::cerr << "http2 connection step=" << step << '\n';
    return false;
  };
  ntl::net::inspection::content_decoder_registry decoders;
  ntl::net::inspection::content_encoder_registry encoders;
  ntl::net::inspection::register_standard_content_decoders(decoders);
  ntl::net::inspection::register_standard_content_encoders(encoders);
  ntl::net::http::transform_pipeline pipeline;
  pipeline.requests().transform(
      [](ntl::net::http::request_message &message) {
        message.headers.set("x-connection-policy", "yes");
        return ntl::net::http::rewrite_result::
            headers_changed();
      });
  pipeline.responses().html().transform(
      [](const ntl::net::http::request_message &,
         ntl::net::http::response_message &message) {
        std::string rewritten = text(message.body);
        rewritten.append("<!-- complete-stream -->");
        return ntl::net::http::rewrite_result::replace_text(
            std::move(rewritten));
      });

  auto exchanges = std::make_shared<ntl::net::http2::exchange_store>();
  ntl::net::http2::connection_transformer requests(
      ntl::net::http2::connection_direction::requests,
      exchanges, pipeline, decoders, encoders, 7);
  ntl::net::http2::connection_transformer responses(
      ntl::net::http2::connection_direction::responses,
      exchanges, pipeline, decoders, encoders, 11);
  exchanges.reset();

  auto semantic_request = request();
  semantic_request.headers.set("content-length", "0");
  auto request_frames =
      ntl::net::http2::encode_request_frames(
          3, semantic_request, {}, 5);
  if (!request_frames)
    return failed(1);
  ntl::net::http2::connection_transform_result
      completed_request;
  std::size_t request_frame_index = 0;
  for (const auto &encoded : *request_frames) {
    auto frame = ntl::net::http2::frame_view::parse(
        ntl::net::scatter_view::from_contiguous(encoded.wire),
        {64, false});
    if (!frame)
      return failed(2);
    auto transformed = requests.consume(*frame);
    if (!transformed) {
      std::cerr << "request frame=" << request_frame_index
                << " type="
                << static_cast<unsigned>(frame->header().type)
                << " flags="
                << static_cast<unsigned>(frame->header().flags)
                << " request status="
                << static_cast<unsigned long>(
                       static_cast<NTSTATUS>(
                           transformed.status()))
                << '\n';
      return failed(3);
    }
    if (transformed->message_complete)
      completed_request = std::move(*transformed);
    ++request_frame_index;
  }
  if (!completed_request.message_complete ||
      !completed_request.request ||
      completed_request.forward.empty() ||
      completed_request.request->headers.first(
          "x-connection-policy") != "yes")
    return failed(4);

  auto semantic_response = response();
  semantic_response.headers.set(
      "content-length",
      std::to_string(semantic_response.body.size()));
  auto response_frames =
      ntl::net::http2::encode_response_frames(
          3, semantic_response, semantic_response.body, 9);
  if (!response_frames)
    return failed(5);
  ntl::net::http2::connection_transform_result
      completed_response;
  std::size_t credited = 0;
  for (const auto &encoded : *response_frames) {
    auto frame = ntl::net::http2::frame_view::parse(
        ntl::net::scatter_view::from_contiguous(encoded.wire),
        {64, false});
    if (!frame)
      return failed(6);
    auto transformed = responses.consume(*frame);
    if (!transformed) {
      std::cerr << "response status="
                << static_cast<unsigned long>(
                       static_cast<NTSTATUS>(
                           transformed.status()))
                << '\n';
      return failed(7);
    }
    credited +=
        transformed->received_flow_controlled_bytes;
    if (transformed->message_complete)
      completed_response = std::move(*transformed);
  }
  if (!completed_response.message_complete)
    return failed(8);
  if (!completed_response.response)
    return failed(9);
  if (completed_response.forward.size() < 2)
    return failed(10);
  if (credited != semantic_response.body.size()) {
    std::cerr << "credited=" << credited
              << " expected=" << semantic_response.body.size()
              << '\n';
    return failed(11);
  }
  if (!text(completed_response.response->body).ends_with(
          "<!-- complete-stream -->"))
    return failed(12);

  ntl::net::http::transform_pipeline blocking;
  blocking.requests().decide(
      [](const ntl::net::http::request_message &) {
        return ntl::net::inspection::verdict::block;
      });
  auto blocked_exchanges =
      std::make_shared<ntl::net::http2::exchange_store>();
  ntl::net::http2::connection_transformer blocker(
      ntl::net::http2::connection_direction::requests,
      blocked_exchanges, blocking, decoders, encoders);
  auto blocked_frames =
      ntl::net::http2::encode_request_frames(
          5, semantic_request, {});
  if (!blocked_frames)
    return failed(13);
  ntl::net::http2::connection_transform_result blocked;
  for (const auto &encoded : *blocked_frames) {
    auto frame = ntl::net::http2::frame_view::parse(
        ntl::net::scatter_view::from_contiguous(encoded.wire),
        {64 * 1024, false});
    if (!frame)
      return failed(14);
    auto transformed = blocker.consume(*frame);
    if (!transformed)
      return failed(15);
    if (transformed->message_complete)
      blocked = std::move(*transformed);
  }
  if (blocked.terminal_status != 403 ||
      blocked.reverse.empty() ||
      !blocked.forward.empty())
    return failed(16);
  return true;
}

bool test_http1_request_target_and_transfer_coding() {
  ntl::net::inspection::content_decoder_registry decoders;
  ntl::net::inspection::content_encoder_registry encoders;
  ntl::net::http::transform_pipeline pipeline;

  const auto origin = ntl::net::http::parse_http1_request(
      bytes("GET /items?q=1 HTTP/1.1\r\nHost: example.test\r\n\r\n"),
      decoders, {.origin_scheme = "http"});
  if (!origin || origin->message.scheme != "http" ||
      origin->message.authority != "example.test" ||
      origin->message.path != "/items?q=1")
    return false;

  const auto absolute_wire = bytes(
      "GET https://Example.Test:443/items?q=1 HTTP/1.1\r\n"
      "Host: example.test:443\r\n\r\n");
  const auto absolute = ntl::net::http::transform_http1_request(
      absolute_wire, pipeline, decoders, encoders,
      {.origin_scheme = "https"});
  if (!absolute || !absolute->absolute_form ||
      absolute->message.scheme != "https" ||
      absolute->message.authority != "Example.Test:443" ||
      absolute->message.path != "/items?q=1" ||
      !text(absolute->wire).starts_with("GET /items?q=1 HTTP/1.1\r\n"))
    return false;

  const auto duplicate_host = ntl::net::http::parse_http1_request(
      bytes("GET / HTTP/1.1\r\nHost: one.test\r\nHost: two.test\r\n\r\n"),
      decoders, {.origin_scheme = "https"});
  const auto mismatched_authority = ntl::net::http::parse_http1_request(
      bytes("GET https://one.test/ HTTP/1.1\r\nHost: two.test\r\n\r\n"),
      decoders, {.origin_scheme = "https"});
  const auto mismatched_scheme = ntl::net::http::parse_http1_request(
      bytes("GET http://example.test/ HTTP/1.1\r\nHost: example.test\r\n\r\n"),
      decoders, {.origin_scheme = "https"});
  const auto missing_transport_scheme = ntl::net::http::parse_http1_request(
      bytes("GET / HTTP/1.1\r\nHost: example.test\r\n\r\n"), decoders,
      {.origin_scheme = {}});
  if (duplicate_host || mismatched_authority || mismatched_scheme ||
      missing_transport_scheme)
    return false;

  const auto unsupported_request_coding =
      ntl::net::http::parse_http1_request(
          bytes("POST / HTTP/1.1\r\nHost: example.test\r\n"
                "Transfer-Encoding: gzip, chunked\r\n\r\n"
                "1\r\nx\r\n0\r\n\r\n"),
          decoders, {.origin_scheme = "https"});
  const auto unsupported_response_coding =
      ntl::net::http::parse_http1_response(
          bytes("HTTP/1.1 200 OK\r\n"
                "Transfer-Encoding: gzip, chunked\r\n\r\n"
                "1\r\nx\r\n0\r\n\r\n"),
          decoders);
  return !unsupported_request_coding &&
         unsupported_request_coding.status() == STATUS_NOT_SUPPORTED &&
         !unsupported_response_coding &&
         unsupported_response_coding.status() == STATUS_NOT_SUPPORTED;
}

bool test_http2_extended_connect_transformer() {
  ntl::net::inspection::content_decoder_registry decoders;
  ntl::net::inspection::content_encoder_registry encoders;
  ntl::net::http::transform_pipeline pipeline;
  auto exchanges = std::make_shared<ntl::net::http2::exchange_store>();
  exchanges->peer_extended_connect_enabled(true);
  ntl::net::http2::connection_transformer requests(
      ntl::net::http2::connection_direction::requests,
      exchanges, pipeline, decoders, encoders);
  ntl::net::http2::connection_transformer responses(
      ntl::net::http2::connection_direction::responses,
      exchanges, pipeline, decoders, encoders);

  ntl::net::http::request_message connect;
  connect.wire_protocol = ntl::net::http::protocol::http2;
  connect.method = "CONNECT";
  connect.scheme = "https";
  connect.authority = "example.test";
  connect.path = "/chat";
  connect.extended_protocol = "websocket";
  connect.headers.append("origin", "https://example.test");
  auto opening = ntl::net::http2::encode_request_frames(
      7, connect, {}, 16 * 1024, 256 * 1024, false);
  if (!opening)
    return false;
  ntl::net::http2::connection_transform_result opened_request;
  for (const auto &encoded : *opening) {
    auto frame = ntl::net::http2::frame_view::parse(
        ntl::net::scatter_view::from_contiguous(encoded.wire));
    if (!frame || frame->header().end_stream())
      return false;
    auto transformed = requests.consume(*frame);
    if (!transformed)
      return false;
    if (transformed->message_complete)
      opened_request = std::move(*transformed);
  }
  if (!opened_request.message_complete ||
      !opened_request.request || opened_request.forward.empty() ||
      exchanges->is_connect_admitted(7) ||
      !exchanges->admit_connect(
           7, ntl::net::http2::connect_disposition::inspect)
           .is_ok() ||
      !exchanges->is_connect_admitted(7))
    return false;

  std::vector<ntl::net::http2::outbound_frame> request_data;
  if (!ntl::net::http2::transform_detail::append_data_frames(
           request_data, 7, bytes("client tunnel"), false,
           16 * 1024)
           .is_ok())
    return false;
  auto request_data_frame = ntl::net::http2::frame_view::parse(
      ntl::net::scatter_view::from_contiguous(
          request_data.front().wire));
  if (!request_data_frame)
    return false;
  auto request_passthrough = requests.consume(*request_data_frame);
  if (!request_passthrough || request_passthrough->consumed)
    return false;

  // A tunnel DATA frame is still governed by both peer send windows. Tiny
  // asymmetric windows exercise the exact path that an opaque passthrough
  // adapter must reserve before writing in each direction.
  ntl::net::http2::send_window client_to_origin_window(2, 2);
  const auto request_credit = static_cast<std::uint32_t>(
      request_data_frame->header().payload_size);
  if (client_to_origin_window.try_reserve(7, request_credit) ||
      !client_to_origin_window.update(0, request_credit - 2) ||
      !client_to_origin_window.update(7, request_credit - 2) ||
      !client_to_origin_window.try_reserve(7, request_credit))
    return false;

  ntl::net::http::response_message accepted;
  accepted.wire_protocol = ntl::net::http::protocol::http2;
  accepted.status = 200;
  auto response_opening = ntl::net::http2::encode_response_frames(
      7, accepted, {}, 16 * 1024, 256 * 1024, true, false);
  if (!response_opening)
    return false;
  ntl::net::http2::connection_transform_result opened_response;
  for (const auto &encoded : *response_opening) {
    auto frame = ntl::net::http2::frame_view::parse(
        ntl::net::scatter_view::from_contiguous(encoded.wire));
    if (!frame || frame->header().end_stream())
      return false;
    auto transformed = responses.consume(*frame);
    if (!transformed)
      return false;
    if (transformed->message_complete)
      opened_response = std::move(*transformed);
  }
  if (!opened_response.message_complete ||
      !opened_response.response || opened_response.forward.empty() ||
      !exchanges->is_tunnel(7))
    return false;

  std::vector<ntl::net::http2::outbound_frame> response_data;
  if (!ntl::net::http2::transform_detail::append_data_frames(
           response_data, 7, bytes("server tunnel"), true,
           16 * 1024)
           .is_ok())
    return false;
  auto response_data_frame = ntl::net::http2::frame_view::parse(
      ntl::net::scatter_view::from_contiguous(
          response_data.front().wire));
  if (!response_data_frame)
    return false;
  ntl::net::http2::send_window origin_to_client_window(1, 3);
  const auto response_credit = static_cast<std::uint32_t>(
      response_data_frame->header().payload_size);
  if (origin_to_client_window.try_reserve(7, response_credit) ||
      !origin_to_client_window.update(0, response_credit - 1) ||
      !origin_to_client_window.update(7, response_credit - 3) ||
      !origin_to_client_window.try_reserve(7, response_credit))
    return false;
  auto response_passthrough = responses.consume(*response_data_frame);
  if (!response_passthrough || response_passthrough->consumed ||
      !exchanges->is_tunnel(7))
    return false;
  // The session releases state only after this final DATA frame has been
  // transformed and written; the transformer cannot erase it beforehand.
  exchanges->erase(7);
  return !exchanges->is_tunnel(7);
}

bool test_stream_transform() {
  using namespace ntl::net::http;
  stream_transform_pipeline pipeline;
  pipeline.chunks().transform(
      [](const stream_message_context_view &, const stream_chunk_view &chunk) {
        std::vector<std::byte> replacement(
            chunk.bytes.begin(), chunk.bytes.end());
        for (auto &value : replacement) {
          const auto character = std::to_integer<unsigned char>(value);
          if (character >= 'a' && character <= 'z')
            value = static_cast<std::byte>(character - 'a' + 'A');
        }
        return stream_rewrite_result::replace(std::move(replacement));
      });

  for (const auto protocol : {protocol::http1, protocol::http2,
                              protocol::http3}) {
    auto incoming = request();
    incoming.wire_protocol = protocol;
    auto outgoing = response();
    outgoing.wire_protocol = protocol;
    outgoing.headers.set("content-length", "4");
    if (!pipeline.prepare_headers(incoming, outgoing).is_ok() ||
        outgoing.headers.contains("content-length"))
      return false;
    if (protocol == protocol::http1) {
      if (outgoing.headers.first("transfer-encoding") != "chunked")
        return false;
    } else if (outgoing.headers.contains("transfer-encoding")) {
      return false;
    }

    auto opened = pipeline.open(incoming, outgoing);
    if (!opened)
      return false;
    auto session = std::move(*opened);
    const auto first = session.consume(bytes("ab"), false);
    const auto second = session.consume(bytes("cd"), true);
    if (first.action != stream_rewrite_action::forward ||
        second.action != stream_rewrite_action::forward ||
        !first.modified || !second.modified ||
        text(first.bytes) != "AB" || text(second.bytes) != "CD" ||
        !session.finished() || session.input_bytes() != 4 ||
        session.output_bytes() != 4)
      return false;
  }

  stream_transform_limits limits;
  limits.maximum_input_chunk_bytes = 2;
  limits.maximum_output_chunk_bytes = 2;
  stream_transform_pipeline bounded(limits);
  auto incoming = request();
  auto opened = bounded.open(incoming);
  if (!opened)
    return false;
  auto session = std::move(*opened);
  const auto oversized = session.consume(bytes("abc"), true);
  return oversized.action == stream_rewrite_action::block &&
         oversized.failure == STATUS_BUFFER_OVERFLOW;
}

bool test_stateful_stream_transform() {
  using namespace ntl::net::http;
  stream_transform_pipeline pipeline;
  pipeline.chunks().transform_session(
      [](const stream_message_context_view &) {
        return [expected_offset = std::uint64_t{0}](
                   const stream_message_context_view &,
                   const stream_chunk_view &chunk) mutable {
          if (chunk.input_offset != expected_offset)
            return stream_rewrite_result::block();
          expected_offset += chunk.bytes.size();
          std::vector<std::byte> output(
              chunk.bytes.begin(), chunk.bytes.end());
          output.push_back(static_cast<std::byte>(
              expected_offset & 0xffu));
          return stream_rewrite_result::replace(
              std::move(output));
        };
      });

  auto first_request = request();
  auto second_request = request();
  second_request.path = "/second";
  auto first_opened = pipeline.open(first_request);
  auto second_opened = pipeline.open(second_request);
  if (!first_opened || !second_opened)
    return false;
  auto first = std::move(*first_opened);
  auto second = std::move(*second_opened);

  const auto first_a = first.consume(bytes("ab"), false);
  const auto second_a = second.consume(bytes("x"), false);
  const auto first_b = first.consume(bytes("c"), true);
  const auto second_b = second.consume(bytes("yz"), true);
  return first_a.action == stream_rewrite_action::forward &&
         second_a.action == stream_rewrite_action::forward &&
         first_b.action == stream_rewrite_action::forward &&
         second_b.action == stream_rewrite_action::forward &&
         first_a.bytes.back() == std::byte{2} &&
         second_a.bytes.back() == std::byte{1} &&
         first_b.bytes.back() == std::byte{3} &&
         second_b.bytes.back() == std::byte{3} &&
         first.finished() && second.finished();
}

bool test_http1_streaming_message_transformer() {
  using namespace ntl::net;
  using namespace ntl::net::http;
  inspection::content_decoder_registry decoders;
  inspection::content_encoder_registry encoders;
  inspection::register_standard_content_decoders(decoders);
  inspection::register_standard_content_encoders(encoders);

  stream_transform_limits stream_limits;
  stream_limits.maximum_input_chunk_bytes = 7;
  stream_limits.maximum_output_chunk_bytes = 256 * 1024;
  stream_limits.maximum_codec_chunk_bytes = 256 * 1024;
  stream_limits.maximum_content_expansion_ratio = 2048;
  stream_transform_pipeline pipeline(stream_limits);
  pipeline.chunks().transform(
      [](const stream_message_context_view &, const stream_chunk_view &chunk) {
        std::vector<std::byte> output(chunk.bytes.begin(), chunk.bytes.end());
        for (auto &value : output) {
          const auto character = std::to_integer<unsigned char>(value);
          if (character >= 'a' && character <= 'z')
            value = static_cast<std::byte>(character - 'a' + 'A');
        }
        return stream_rewrite_result::replace(std::move(output));
      });

  const auto make_expected = [] {
    auto expected = bytes("abcdefghijklmnopqrstuvwxyzabcdefghijklmnopqrstuvwxyz");
    for (auto &value : expected) {
      const auto character = std::to_integer<unsigned char>(value);
      value = static_cast<std::byte>(character - 'a' + 'A');
    }
    return expected;
  };
  const auto plain = bytes("abcdefghijklmnopqrstuvwxyzabcdefghijklmnopqrstuvwxyz");
  const auto expected = make_expected();

  const auto encode = [&](std::string_view coding) {
    if (coding.empty())
      return std::optional<std::vector<std::byte>>(plain);
    auto encoded = inspection::encode_content_encoding(
        encoders, plain, coding,
        {.maximum_input_size = plain.size(),
         .maximum_encoded_size = 256 * 1024,
         .maximum_coding_layers = 2});
    return encoded ? std::optional<std::vector<std::byte>>(std::move(*encoded))
                   : std::nullopt;
  };

  const auto fixed = [&](std::string_view coding) {
    auto encoded = encode(coding);
    if (!encoded)
      return false;
    std::string head =
        "POST /upload HTTP/1.1\r\nHost: example.test\r\nContent-Length: " +
        std::to_string(encoded->size()) + "\r\n";
    if (!coding.empty())
      head += "Content-Encoding: " + std::string(coding) + "\r\n";
    head += "\r\n";
    http1_streaming_message_transformer transformer(
        http1_request_stream, pipeline, decoders, encoders,
        {.origin_scheme = "https"},
        {.maximum_encoded_body_bytes = 256 * 1024,
         .maximum_decoded_body_bytes = 256 * 1024,
         .maximum_expansion_ratio = 2048},
        {.maximum_header_size = 64 * 1024,
         .maximum_body_size = 256 * 1024});
    auto first = transformer.consume(bytes(head));
    if (!first || !first->head_forwarded || first->message_complete ||
        first->wire.empty())
      return false;
    std::vector<std::byte> output = std::move(first->wire);
    constexpr std::string_view suffix = "NEXT";
    for (std::size_t offset = 0; offset != encoded->size();) {
      const std::size_t count =
          (std::min)(std::size_t{3}, encoded->size() - offset);
      std::vector<std::byte> input(
          encoded->begin() + static_cast<std::ptrdiff_t>(offset),
          encoded->begin() + static_cast<std::ptrdiff_t>(offset + count));
      const bool last = offset + count == encoded->size();
      if (last) {
        const auto extra = bytes(suffix);
        input.insert(input.end(), extra.begin(), extra.end());
      }
      auto event = transformer.consume(input);
      if (!event || event->action != stream_rewrite_action::forward ||
          event->failure != STATUS_SUCCESS)
        return false;
      output.insert(output.end(), event->wire.begin(), event->wire.end());
      if (last) {
        if (!event->message_complete ||
            event->unconsumed != bytes(suffix))
          return false;
      }
      offset += count;
    }
    auto parsed = parse_http1_request(
        output, decoders, {.origin_scheme = "https"},
        {.maximum_encoded_body_bytes = 256 * 1024,
         .maximum_decoded_body_bytes = 256 * 1024,
         .maximum_expansion_ratio = 2048});
    return parsed && parsed->chunked &&
           !parsed->message.headers.first("content-length") &&
           parsed->message.body == expected;
  };

  const auto chunked = [&] {
    auto encoded = encode("gzip");
    if (!encoded)
      return false;
    std::string head =
        "POST /upload HTTP/1.1\r\nHost: example.test\r\n"
        "Transfer-Encoding: chunked\r\nContent-Encoding: gzip\r\n\r\n";
    std::vector<std::byte> input = bytes(head);
    for (std::size_t offset = 0; offset != encoded->size();) {
      const std::size_t count =
          (std::min)(std::size_t{5}, encoded->size() - offset);
      char size_text[32]{};
      const auto converted = std::to_chars(
          size_text, size_text + sizeof(size_text), count, 16);
      const auto line = bytes(std::string_view(size_text, converted.ptr));
      input.insert(input.end(), line.begin(), line.end());
      const auto crlf = bytes("\r\n");
      input.insert(input.end(), crlf.begin(), crlf.end());
      input.insert(
          input.end(),
          encoded->begin() + static_cast<std::ptrdiff_t>(offset),
          encoded->begin() + static_cast<std::ptrdiff_t>(offset + count));
      input.insert(input.end(), crlf.begin(), crlf.end());
      offset += count;
    }
    const auto terminal = bytes("0\r\nx-check: yes\r\n\r\n");
    input.insert(input.end(), terminal.begin(), terminal.end());

    http1_streaming_message_transformer transformer(
        http1_request_stream, pipeline, decoders, encoders,
        {.origin_scheme = "https"},
        {.maximum_encoded_body_bytes = 256 * 1024,
         .maximum_decoded_body_bytes = 256 * 1024,
         .maximum_expansion_ratio = 2048},
        {.maximum_header_size = 64 * 1024,
         .maximum_body_size = 256 * 1024});
    std::vector<std::byte> output;
    bool complete = false;
    for (std::size_t offset = 0; offset != input.size();) {
      const std::size_t count =
          (std::min)(std::size_t{2}, input.size() - offset);
      auto event = transformer.consume(
          std::span<const std::byte>(input).subspan(offset, count));
      if (!event || event->action != stream_rewrite_action::forward) {
        std::cerr << "http1 chunk event offset=" << offset
                  << " input-size=" << input.size()
                  << " status="
                  << (event ? event->failure
                            : static_cast<NTSTATUS>(event.status()))
                  << '\n';
        return false;
      }
      output.insert(output.end(), event->wire.begin(), event->wire.end());
      complete = complete || event->message_complete;
      offset += count;
    }
    auto parsed = parse_http1_request(
        output, decoders, {.origin_scheme = "https"},
        {.maximum_encoded_body_bytes = 256 * 1024,
         .maximum_decoded_body_bytes = 256 * 1024,
         .maximum_expansion_ratio = 2048});
    if (!complete || !parsed || parsed->message.body != expected ||
        parsed->message.trailers.size() != 1) {
      std::cerr << "http1 chunk result complete=" << complete
                << " parsed=" << static_cast<bool>(parsed)
                << " output=" << output.size()
                << " body=" << (parsed ? parsed->message.body.size() : 0)
                << " trailers="
                << (parsed ? parsed->message.trailers.size() : 0) << '\n';
      return false;
    }
    return
           parsed->message.trailers.size() == 1 &&
           parsed->message.trailers.front().name == "x-check" &&
           parsed->message.trailers.front().value == "yes";
  };

  const auto close_delimited = [&] {
    auto encoded = encode("br");
    if (!encoded)
      return false;
    request_message request_message;
    request_message.method = "GET";
    request_message.authority = "example.test";
    request_message.path = "/";
    std::string head =
        "HTTP/1.1 200 OK\r\nContent-Type: text/plain\r\n"
        "Content-Encoding: br\r\n\r\n";
    http1_streaming_message_transformer transformer(
        http1_response_stream, request_message, pipeline, decoders, encoders,
        {.maximum_encoded_body_bytes = 256 * 1024,
         .maximum_decoded_body_bytes = 256 * 1024,
         .maximum_expansion_ratio = 2048},
        {.maximum_header_size = 64 * 1024,
         .maximum_body_size = 256 * 1024});
    auto head_event = transformer.consume(bytes(head));
    if (!head_event || !head_event->head_forwarded)
      return false;
    std::vector<std::byte> output = std::move(head_event->wire);
    const std::size_t split = encoded->size() / 2;
    auto first = transformer.consume(
        std::span<const std::byte>(*encoded).first(split));
    auto second = transformer.consume(
        std::span<const std::byte>(*encoded).subspan(split), true);
    if (!first || !second || !second->message_complete)
      return false;
    output.insert(output.end(), first->wire.begin(), first->wire.end());
    output.insert(output.end(), second->wire.begin(), second->wire.end());
    auto parsed = parse_http1_response(
        output, decoders,
        {.maximum_encoded_body_bytes = 256 * 1024,
         .maximum_decoded_body_bytes = 256 * 1024,
         .maximum_expansion_ratio = 2048});
    return parsed && parsed->chunked && parsed->message.body == expected;
  };

  const bool fixed_identity = fixed("");
  const bool fixed_gzip = fixed("gzip");
  const bool fixed_br = fixed("br");
  const bool chunked_gzip = chunked();
  const bool close_br = close_delimited();
  const auto truncated_fixed = [&] {
    http1_streaming_message_transformer transformer(
        http1_request_stream, pipeline, decoders, encoders,
        {.origin_scheme = "https"},
        {.maximum_encoded_body_bytes = 256 * 1024,
         .maximum_decoded_body_bytes = 256 * 1024,
         .maximum_expansion_ratio = 2048},
        {.maximum_header_size = 64 * 1024,
         .maximum_body_size = 256 * 1024});
    auto result = transformer.consume(
        bytes("POST / HTTP/1.1\r\nHost: example.test\r\n"
              "Content-Length: 4\r\n\r\nab"),
        true);
    return !result && result.status() == STATUS_DATA_ERROR;
  }();
  const auto truncated_chunked = [&] {
    http1_streaming_message_transformer transformer(
        http1_request_stream, pipeline, decoders, encoders,
        {.origin_scheme = "https"},
        {.maximum_encoded_body_bytes = 256 * 1024,
         .maximum_decoded_body_bytes = 256 * 1024,
         .maximum_expansion_ratio = 2048},
        {.maximum_header_size = 64 * 1024,
         .maximum_body_size = 256 * 1024});
    auto result = transformer.consume(
        bytes("POST / HTTP/1.1\r\nHost: example.test\r\n"
              "Transfer-Encoding: chunked\r\n\r\n4\r\nab"),
        true);
    return !result && result.status() == STATUS_DATA_ERROR;
  }();
  const auto rejected_head = [&](std::string_view wire,
                                 NTSTATUS expected_status) {
    http1_streaming_message_transformer transformer(
        http1_request_stream, pipeline, decoders, encoders,
        {.origin_scheme = "https"});
    const auto result = transformer.consume(bytes(wire), true);
    return !result && result.status() == expected_status;
  };
  const bool duplicate_host = rejected_head(
      "GET / HTTP/1.1\r\nHost: one.test\r\nHost: two.test\r\n\r\n",
      STATUS_DATA_ERROR);
  const bool unsupported_transfer = rejected_head(
      "POST / HTTP/1.1\r\nHost: example.test\r\n"
      "Transfer-Encoding: gzip, chunked\r\n\r\n"
      "1\r\nx\r\n0\r\n\r\n",
      STATUS_NOT_SUPPORTED);
  const auto absolute_form = [&] {
    http1_streaming_message_transformer transformer(
        http1_request_stream, pipeline, decoders, encoders,
        {.origin_scheme = "https"});
    const auto result = transformer.consume(
        bytes("POST https://Example.Test:443/upload?q=1 HTTP/1.1\r\n"
              "Host: example.test:443\r\nContent-Length: 1\r\n\r\nx"),
        true);
    return result && result->request &&
           result->request->scheme == "https" &&
           result->request->authority == "Example.Test:443" &&
           result->request->path == "/upload?q=1" &&
           text(result->wire).starts_with(
               "POST /upload?q=1 HTTP/1.1\r\n");
  }();
  if (!fixed_identity || !fixed_gzip || !fixed_br ||
      !chunked_gzip || !close_br || !truncated_fixed ||
      !truncated_chunked || !duplicate_host || !unsupported_transfer ||
      !absolute_form)
    std::cerr << "http1 stream fixed=" << fixed_identity
              << " gzip=" << fixed_gzip
              << " br=" << fixed_br
              << " chunked=" << chunked_gzip
              << " close=" << close_br
              << " truncated-fixed=" << truncated_fixed
              << " truncated-chunked=" << truncated_chunked
              << " duplicate-host=" << duplicate_host
              << " unsupported-transfer=" << unsupported_transfer
              << " absolute-form=" << absolute_form << '\n';
  return fixed_identity && fixed_gzip && fixed_br &&
         chunked_gzip && close_br && truncated_fixed &&
         truncated_chunked && duplicate_host && unsupported_transfer &&
         absolute_form;
}

bool test_http2_streaming_connection_transformer() {
  using namespace ntl::net;
  using namespace ntl::net::http;
  inspection::content_decoder_registry decoders;
  inspection::content_encoder_registry encoders;
  inspection::register_standard_content_decoders(decoders);
  inspection::register_standard_content_encoders(encoders);

  stream_transform_limits stream_limits;
  stream_limits.maximum_input_chunk_bytes = 7;
  stream_limits.maximum_output_chunk_bytes = 256 * 1024;
  stream_limits.maximum_codec_chunk_bytes = 256 * 1024;
  stream_limits.maximum_content_expansion_ratio = 2048;
  stream_transform_pipeline pipeline(stream_limits);
  pipeline.chunks().transform(
      [](const stream_message_context_view &, const stream_chunk_view &chunk) {
        std::vector<std::byte> output(
            chunk.bytes.begin(), chunk.bytes.end());
        for (auto &value : output) {
          const auto character = std::to_integer<unsigned char>(value);
          if (character >= 'a' && character <= 'z')
            value = static_cast<std::byte>(character - 'a' + 'A');
        }
        return stream_rewrite_result::replace(std::move(output));
      });

  const auto exercise = [&](std::string_view coding) {
    const auto plain = bytes(
        "abcdefghijklmnopqrstuvwxyzabcdefghijklmnopqrstuvwxyz");
    std::vector<std::byte> expected = plain;
    for (auto &value : expected) {
      const auto character = std::to_integer<unsigned char>(value);
      value = static_cast<std::byte>(character - 'a' + 'A');
    }
    std::vector<std::byte> wire = plain;
    if (!coding.empty()) {
      auto encoded = inspection::encode_content_encoding(
          encoders, plain, coding,
          {.maximum_input_size = plain.size(),
           .maximum_encoded_size = 256 * 1024,
           .maximum_coding_layers = 2});
      if (!encoded)
        return false;
      wire = std::move(*encoded);
    }

    request_message semantic = request();
    semantic.wire_protocol = protocol::http2;
    semantic.method = "POST";
    semantic.headers.set("content-length", std::to_string(wire.size()));
    if (!coding.empty())
      semantic.headers.set("content-encoding", std::string(coding));
    auto frames = ntl::net::http2::encode_request_frames(
        1, semantic, wire, 7);
    if (!frames)
      return false;

    auto exchanges = std::make_shared<ntl::net::http2::exchange_store>();
    ntl::net::http2::streaming_connection_transformer transformer(
        ntl::net::http2::connection_direction::requests,
        exchanges, pipeline, decoders, encoders, {}, 7);
    std::vector<std::byte> transformed_wire;
    bool initial_headers_forwarded = false;
    bool completed = false;
    for (const auto &encoded : *frames) {
      auto incoming = ntl::net::http2::frame_view::parse(
          scatter_view::from_contiguous(encoded.wire),
          {64 * 1024, false});
      if (!incoming)
        return false;
      auto outcome = transformer.consume(*incoming);
      if (!outcome || !outcome->consumed ||
          outcome->action != stream_rewrite_action::forward ||
          outcome->failure != STATUS_SUCCESS)
        return false;
      if (incoming->header().type ==
              ntl::net::http2::frame_type::data &&
          coding.empty() && outcome->forward.empty())
        return false;
      for (const auto &forwarded : outcome->forward) {
        auto frame = ntl::net::http2::frame_view::parse(
            scatter_view::from_contiguous(forwarded.wire),
            {64 * 1024, false});
        if (!frame)
          return false;
        if (frame->header().type ==
            ntl::net::http2::frame_type::headers)
          initial_headers_forwarded = true;
        if (frame->header().type !=
            ntl::net::http2::frame_type::data)
          continue;
        auto payload = frame->data_payload();
        if (!payload)
          return false;
        const std::size_t offset = transformed_wire.size();
        transformed_wire.resize(offset + payload->size());
        if (!payload->copy_to(
                std::span<std::byte>(transformed_wire).subspan(offset))
                 .is_ok())
          return false;
      }
      completed = completed || outcome->stream_complete;
    }
    if (!initial_headers_forwarded || !completed)
      return false;
    if (coding.empty())
      return transformed_wire == expected;
    auto decoded = inspection::decode_content_encoding(
        decoders,
        scatter_view::from_contiguous(
            std::span<const std::byte>(transformed_wire)),
        coding,
        {.maximum_encoded_size = transformed_wire.size(),
         .maximum_decoded_size = expected.size(),
         .maximum_expansion_ratio = 2048,
         .maximum_coding_layers = 2});
    return decoded && *decoded == expected;
  };

  if (!exercise("") || !exercise("gzip") || !exercise("br"))
    return false;

  request_message with_trailer = request();
  with_trailer.wire_protocol = protocol::http2;
  with_trailer.method = "POST";
  with_trailer.body = bytes("x");
  with_trailer.headers.set("content-length", "1");
  with_trailer.trailers.push_back({"x-check", "yes", false});
  auto frames = ntl::net::http2::encode_request_frames(
      3, with_trailer, with_trailer.body, 16);
  if (!frames)
    return false;
  auto trailer_exchanges =
      std::make_shared<ntl::net::http2::exchange_store>();
  ntl::net::http2::streaming_connection_transformer trailer_transformer(
      ntl::net::http2::connection_direction::requests,
      trailer_exchanges, pipeline, decoders, encoders, {}, 16);
  std::optional<ntl::net::http::request_message> completed;
  for (const auto &encoded : *frames) {
    auto frame = ntl::net::http2::frame_view::parse(
        scatter_view::from_contiguous(encoded.wire), {64 * 1024, false});
    if (!frame)
      return false;
    auto event = trailer_transformer.consume(*frame);
    if (!event)
      return false;
    if (event->stream_complete)
      completed = std::move(event->request);
  }
  if (!completed || completed->trailers.size() != 1 ||
      completed->trailers.front().name != "x-check" ||
      completed->trailers.front().value != "yes")
    return false;

  request_message invalid = request();
  invalid.wire_protocol = protocol::http2;
  invalid.method = "POST";
  invalid.headers.set("content-length", "1");
  auto initial = ntl::net::http2::encode_request_frames(
      5, invalid, bytes("x"), 16);
  if (!initial || initial->empty())
    return false;
  auto initial_frame = ntl::net::http2::frame_view::parse(
      scatter_view::from_contiguous(initial->front().wire),
      {64 * 1024, false});
  if (!initial_frame)
    return false;
  auto invalid_exchanges =
      std::make_shared<ntl::net::http2::exchange_store>();
  ntl::net::http2::streaming_connection_transformer invalid_transformer(
      ntl::net::http2::connection_direction::requests,
      invalid_exchanges, pipeline, decoders, encoders, {}, 16);
  auto accepted = invalid_transformer.consume(*initial_frame);
  if (!accepted)
    return false;
  std::vector<ntl::net::http2::outbound_frame> invalid_trailer;
  const std::vector<ntl::net::http2::header_field> forbidden{
      {"content-length", "1", false}};
  if (!ntl::net::http2::transform_detail::append_header_frames(
           invalid_trailer, 5, forbidden, true, 16, 64 * 1024)
           .is_ok() ||
      invalid_trailer.size() != 1)
    return false;
  auto forbidden_frame = ntl::net::http2::frame_view::parse(
      scatter_view::from_contiguous(invalid_trailer.front().wire),
      {64 * 1024, false});
  if (!forbidden_frame)
    return false;
  auto rejected = invalid_transformer.consume(*forbidden_frame);
  return !rejected && rejected.status() == STATUS_DATA_ERROR &&
         !invalid_exchanges->request(5);
}

bool test_http3_streaming_connection_transformer() {
  using namespace ntl::net;
  using namespace ntl::net::http;
  inspection::content_decoder_registry decoders;
  inspection::content_encoder_registry encoders;
  inspection::register_standard_content_decoders(decoders);
  inspection::register_standard_content_encoders(encoders);

  const std::vector<ntl::net::http3::header_field> uppercase_request{
      {":method", "GET"},
      {":scheme", "https"},
      {":authority", "example.test"},
      {":path", "/"},
      {"X-Policy", "inspect"}};
  const std::vector<ntl::net::http3::header_field> uppercase_response{
      {":status", "200"}, {"X-Policy", "inspect"}};
  const std::vector<ntl::net::http3::header_field> duplicate_empty_method{
      {":method", ""},
      {":method", "GET"},
      {":scheme", "https"},
      {":authority", "example.test"},
      {":path", "/"}};
  const std::vector<ntl::net::http3::header_field> host_and_authority{
      {":method", "GET"},
      {":scheme", "https"},
      {":authority", "one.example.test"},
      {":path", "/"},
      {"host", "two.example.test"}};
  const std::vector<ntl::net::http3::header_field> switching_protocols{
      {":status", "101"}};
  const auto invalid_request =
      ntl::net::http3::stream_transform_detail::parse_request_head(
          uppercase_request, {});
  const auto invalid_response =
      ntl::net::http3::stream_transform_detail::parse_response_head(
          uppercase_response, {});
  const auto duplicate =
      ntl::net::http3::stream_transform_detail::parse_request_head(
          duplicate_empty_method, {});
  const auto ambiguous =
      ntl::net::http3::stream_transform_detail::parse_request_head(
          host_and_authority, {});
  const auto invalid_upgrade =
      ntl::net::http3::stream_transform_detail::parse_response_head(
          switching_protocols, {});
  if (invalid_request || invalid_request.status() != STATUS_DATA_ERROR ||
      invalid_response || invalid_response.status() != STATUS_DATA_ERROR)
    return false;
  if (duplicate || duplicate.status() != STATUS_DATA_ERROR || ambiguous ||
      ambiguous.status() != STATUS_DATA_ERROR || invalid_upgrade ||
      invalid_upgrade.status() != STATUS_DATA_ERROR)
    return false;

  stream_transform_limits stream_limits;
  stream_limits.maximum_input_chunk_bytes = 7;
  stream_limits.maximum_output_chunk_bytes = 256 * 1024;
  stream_limits.maximum_codec_chunk_bytes = 256 * 1024;
  stream_limits.maximum_content_expansion_ratio = 2048;
  stream_transform_pipeline pipeline(stream_limits);
  pipeline.chunks().transform(
      [](const stream_message_context_view &, const stream_chunk_view &chunk) {
        std::vector<std::byte> output(chunk.bytes.begin(), chunk.bytes.end());
        for (auto &value : output) {
          const auto character = std::to_integer<unsigned char>(value);
          if (character >= 'a' && character <= 'z')
            value = static_cast<std::byte>(character - 'a' + 'A');
        }
        return stream_rewrite_result::replace(std::move(output));
      });

  const auto exercise = [&](std::string_view coding) {
    const auto plain = bytes(
        "abcdefghijklmnopqrstuvwxyzabcdefghijklmnopqrstuvwxyz");
    std::vector<std::byte> expected = plain;
    for (auto &value : expected) {
      const auto character = std::to_integer<unsigned char>(value);
      value = static_cast<std::byte>(character - 'a' + 'A');
    }
    std::vector<std::byte> wire = plain;
    if (!coding.empty()) {
      auto encoded = inspection::encode_content_encoding(
          encoders, plain, coding,
          {.maximum_input_size = plain.size(),
           .maximum_encoded_size = 256 * 1024,
           .maximum_coding_layers = 2});
      if (!encoded)
        return false;
      wire = std::move(*encoded);
    }

    std::vector<ntl::net::http3::header_field> request_fields{
        {":method", "POST"},
        {":scheme", "https"},
        {":authority", "example.test"},
        {":path", "/upload"},
        {"content-length", std::to_string(wire.size())}};
    if (!coding.empty())
      request_fields.push_back(
          {"content-encoding", std::string(coding)});

    auto exchanges =
        std::make_shared<ntl::net::http3::stream_exchange_store>();
    ntl::net::http3::streaming_connection_transformer requests(
        ntl::net::http3::stream_direction::requests,
        exchanges, pipeline, decoders, encoders, {}, 64 * 1024);
    auto head = requests.on_headers(1, request_fields);
    if (!head || head->writes.size() != 1 || head->writes.front().final)
      return false;
    auto head_frame = ntl::net::http3::frame_view::parse(
        scatter_view::from_contiguous(head->writes.front().wire),
        {64 * 1024});
    if (!head_frame ||
        head_frame->header().type() != ntl::net::http3::frame_type::headers)
      return false;
    ntl::net::http3::bounded_static_qpack_decoder qpack;
    auto decoded_head = qpack.decode(1, head_frame->payload(), 64 * 1024);
    if (!decoded_head)
      return false;
    for (const auto &field : decoded_head->fields) {
      if (field.name == "content-length")
        return false;
    }

    std::vector<std::byte> transformed;
    bool final_write = false;
    const auto collect = [&](const ntl::net::http3::stream_event_result &event) {
      for (const auto &write : event.writes) {
        final_write = final_write || write.final;
        if (write.wire.empty())
          continue;
        auto frame = ntl::net::http3::frame_view::parse(
            scatter_view::from_contiguous(write.wire), {64 * 1024});
        if (!frame || frame->header().type() !=
                          ntl::net::http3::frame_type::data)
          return false;
        const std::size_t offset = transformed.size();
        transformed.resize(offset + frame->payload().size());
        if (!frame->payload()
                 .copy_to(std::span<std::byte>(transformed).subspan(offset))
                 .is_ok())
          return false;
      }
      return true;
    };

    for (std::size_t offset = 0; offset != wire.size();) {
      const std::size_t count = (std::min)(std::size_t{7}, wire.size() - offset);
      auto event = requests.on_data(
          1, scatter_view::from_contiguous(
                 std::span<const std::byte>(wire).subspan(offset, count)));
      if (!event || event->action != stream_rewrite_action::forward ||
          event->failure != STATUS_SUCCESS || !collect(*event))
        return false;
      if (coding.empty() && event->writes.empty())
        return false;
      offset += count;
    }
    auto ended = requests.on_stream_end(1);
    if (!ended || !ended->stream_complete || !collect(*ended) || !final_write)
      return false;

    const auto matches = [&] {
      if (coding.empty())
        return transformed == expected;
      auto decoded = inspection::decode_content_encoding(
          decoders,
          scatter_view::from_contiguous(
              std::span<const std::byte>(transformed)),
          coding,
          {.maximum_encoded_size = transformed.size(),
           .maximum_decoded_size = expected.size(),
           .maximum_expansion_ratio = 2048,
           .maximum_coding_layers = 2});
      return decoded && *decoded == expected;
    };
    if (!matches())
      return false;

    std::vector<ntl::net::http3::header_field> response_fields{
        {":status", "200"},
        {"content-type", "application/octet-stream"},
        {"content-length", std::to_string(wire.size())}};
    if (!coding.empty())
      response_fields.push_back(
          {"content-encoding", std::string(coding)});
    ntl::net::http3::streaming_connection_transformer responses(
        ntl::net::http3::stream_direction::responses,
        exchanges, pipeline, decoders, encoders, {}, 64 * 1024);
    auto response_head = responses.on_headers(1, response_fields);
    if (!response_head || response_head->writes.size() != 1)
      return false;
    transformed.clear();
    final_write = false;
    for (std::size_t offset = 0; offset != wire.size();) {
      const std::size_t count =
          (std::min)(std::size_t{7}, wire.size() - offset);
      auto event = responses.on_data(
          1, scatter_view::from_contiguous(
                 std::span<const std::byte>(wire).subspan(offset, count)));
      if (!event || !collect(*event))
        return false;
      offset += count;
    }
    auto response_end = responses.on_stream_end(1);
    if (!response_end || !response_end->stream_complete ||
        !response_end->response || !collect(*response_end) ||
        !final_write || !matches())
      return false;
    return !exchanges->request(1);
  };

  return exercise("") && exercise("gzip") && exercise("br");
}

bool test_compressed_stream_transform() {
  using namespace ntl::net;
  using namespace ntl::net::http;
  const auto failed = [](std::string_view coding, protocol wire_protocol,
                         int step, NTSTATUS status = STATUS_SUCCESS) {
    std::cerr << "compressed stream coding=" << coding
              << " protocol=" << static_cast<unsigned>(wire_protocol)
              << " step=" << step << " status="
              << static_cast<unsigned long>(status) << '\n';
    return false;
  };
  inspection::content_decoder_registry decoders;
  inspection::content_encoder_registry encoders;
  inspection::register_standard_content_decoders(decoders);
  inspection::register_standard_content_encoders(encoders);

  std::vector<std::byte> plain;
  plain.reserve(192 * 1024);
  for (std::size_t index = 0; index != 192 * 1024; ++index)
    plain.push_back(static_cast<std::byte>('a' + (index * 37 + index / 251) % 26));
  std::vector<std::byte> expected = plain;
  for (auto &value : expected) {
    const auto character = std::to_integer<unsigned char>(value);
    if (character >= 'a' && character <= 'z')
      value = static_cast<std::byte>(character - 'a' + 'A');
  }

  for (const std::string_view coding :
       {"gzip", "deflate", "br", "gzip, br"}) {
    auto wire = inspection::encode_content_encoding(
        encoders, plain, coding,
        {.maximum_input_size = plain.size(),
         .maximum_encoded_size = 2 * 1024 * 1024,
         .maximum_coding_layers = 4});
    if (!wire || wire->empty())
      return failed(coding, protocol::http1, 1,
                    wire ? STATUS_DATA_ERROR
                         : static_cast<NTSTATUS>(wire.status()));
    for (const auto protocol :
         {protocol::http1, protocol::http2, protocol::http3}) {
      stream_transform_limits limits;
      limits.maximum_input_chunk_bytes = 1;
      limits.maximum_output_chunk_bytes = 2 * 1024 * 1024;
      limits.maximum_codec_chunk_bytes = 2 * 1024 * 1024;
      limits.maximum_content_expansion_ratio = 2048;
      stream_transform_pipeline pipeline(limits);
      pipeline.chunks().transform(
          [](const stream_message_context_view &, const stream_chunk_view &chunk) {
            std::vector<std::byte> replacement(chunk.bytes.begin(),
                                               chunk.bytes.end());
            for (auto &value : replacement) {
              const auto character = std::to_integer<unsigned char>(value);
              if (character >= 'a' && character <= 'z')
                value = static_cast<std::byte>(character - 'a' + 'A');
            }
            return stream_rewrite_result::replace(std::move(replacement));
          });
      auto incoming = request();
      incoming.wire_protocol = protocol;
      auto outgoing = response();
      outgoing.wire_protocol = protocol;
      outgoing.headers.set("content-encoding", std::string(coding));
      outgoing.headers.set("content-length", std::to_string(wire->size()));
      if (!pipeline.prepare_headers(incoming, outgoing).is_ok() ||
          outgoing.headers.joined("content-encoding") != coding ||
          outgoing.headers.contains("content-length"))
        return failed(coding, protocol, 2);
      auto opened = pipeline.open(incoming, outgoing, decoders, encoders);
      if (!opened)
        return failed(coding, protocol, 3, opened.status());
      auto session = std::move(*opened);
      std::vector<std::byte> transformed_wire;
      for (std::size_t index = 0; index != wire->size(); ++index) {
        const bool final = index + 1 == wire->size();
        const auto outcome = session.consume(
            std::span<const std::byte>(*wire).subspan(index, 1), final);
        if (outcome.action != stream_rewrite_action::forward ||
            outcome.failure != STATUS_SUCCESS || outcome.final != final)
          return failed(coding, protocol, 4, outcome.failure);
        transformed_wire.insert(transformed_wire.end(), outcome.bytes.begin(),
                                outcome.bytes.end());
      }
      if (!session.finished() || session.decoded_bytes() != plain.size() ||
          session.transformed_bytes() != expected.size())
        return failed(coding, protocol, 5);
      auto decoded = inspection::decode_content_encoding(
          decoders,
          scatter_view::from_contiguous(
              std::span<const std::byte>(transformed_wire)),
          coding,
          {.maximum_encoded_size = transformed_wire.size(),
           .maximum_decoded_size = expected.size(),
           .maximum_expansion_ratio = 4096,
           .maximum_coding_layers = 4});
      if (!decoded || *decoded != expected)
        return failed(coding, protocol, 6,
                      decoded ? STATUS_DATA_ERROR
                              : static_cast<NTSTATUS>(decoded.status()));
    }

    stream_transform_limits truncated_limits;
    truncated_limits.maximum_input_chunk_bytes = wire->size();
    truncated_limits.maximum_content_expansion_ratio = 2048;
    stream_transform_pipeline truncated_pipeline(truncated_limits);
    auto incoming = request();
    auto outgoing = response();
    outgoing.headers.set("content-encoding", std::string(coding));
    auto opened = truncated_pipeline.open(incoming, outgoing, decoders,
                                          encoders);
    if (!opened)
      return failed(coding, protocol::http2, 7, opened.status());
    auto truncated = std::move(*opened);
    const auto rejected = truncated.consume(
        std::span<const std::byte>(*wire).first(wire->size() - 1), true);
    if (rejected.action != stream_rewrite_action::block ||
        rejected.failure == STATUS_SUCCESS)
      return failed(coding, protocol::http2, 8, rejected.failure);

    if (coding == "gzip" || coding == "deflate") {
      auto corrupt_wire = *wire;
      corrupt_wire.back() ^= std::byte{0x5a};
      auto corrupt_opened = truncated_pipeline.open(
          incoming, outgoing, decoders, encoders);
      if (!corrupt_opened)
        return failed(coding, protocol::http2, 9,
                      corrupt_opened.status());
      auto corrupt = std::move(*corrupt_opened);
      const auto corrupt_result = corrupt.consume(corrupt_wire, true);
      if (corrupt_result.action != stream_rewrite_action::block ||
          corrupt_result.failure == STATUS_SUCCESS)
        return failed(coding, protocol::http2, 10,
                      corrupt_result.failure);
    }
  }

  const auto repetitive = bytes(std::string(256 * 1024, 'z'));
  auto compressed = inspection::encode_content_encoding(
      encoders, repetitive, "gzip",
      {.maximum_input_size = repetitive.size(),
       .maximum_encoded_size = repetitive.size(),
       .maximum_coding_layers = 1});
  if (!compressed)
    return false;
  stream_transform_limits bomb_limits;
  bomb_limits.maximum_input_chunk_bytes = compressed->size();
  bomb_limits.maximum_content_expansion_ratio = 1;
  bomb_limits.expansion_slack_bytes = 0;
  stream_transform_pipeline bomb_pipeline(bomb_limits);
  auto incoming = request();
  auto outgoing = response();
  outgoing.headers.set("content-encoding", "gzip");
  auto bomb_opened = bomb_pipeline.open(incoming, outgoing, decoders, encoders);
  if (!bomb_opened)
    return false;
  auto bomb = std::move(*bomb_opened);
  const auto rejected = bomb.consume(*compressed, true);
  if (rejected.action != stream_rewrite_action::block ||
      rejected.failure != STATUS_BUFFER_OVERFLOW)
    return false;

  stream_transform_limits unsafe_limits;
  unsafe_limits.on_failure = transform_failure_policy::forward_original;
  try {
    stream_transform_pipeline unsafe(unsafe_limits);
    return false;
  } catch (const std::invalid_argument &) {
    return true;
  }
}

bool test_async_transform() {
  using namespace ntl::net::http;
  const auto failed = [](int step) {
    std::cerr << "async transform step=" << step << '\n';
    return false;
  };
  async_transform_options options;
  options.maximum_concurrency = 2;
  options.maximum_queue_depth = 4;
  options.timeout = std::chrono::milliseconds(250);
  async_transform_policy_builder pipeline_builder({}, options);
  pipeline_builder.requests().transform(
      [](request_message &message, const async_policy_context &context) {
        if (context.cancellation_requested())
          return rewrite_result::block();
        message.headers.set("x-async-policy", "complete");
        return rewrite_result::headers_changed();
      });
  auto pipeline = async_transform_runtime::create(
      std::move(pipeline_builder).build());
  auto incoming = request();
  const auto transformed = apply_async(pipeline, incoming).get();
  if (transformed.action != rewrite_action::forward ||
      !transformed.headers_modified ||
      incoming.headers.first("x-async-policy") != "complete")
    return failed(1);

  async_transform_options timeout_options;
  timeout_options.maximum_concurrency = 1;
  timeout_options.maximum_queue_depth = 2;
  timeout_options.timeout = std::chrono::milliseconds(20);
  async_transform_policy_builder timeout_builder({}, timeout_options);
  timeout_builder.requests().transform(
      [](request_message &, const async_policy_context &context) {
        while (!context.cancellation_requested())
          std::this_thread::sleep_for(std::chrono::milliseconds(1));
        return rewrite_result::unchanged();
      });
  auto timeout_pipeline = async_transform_runtime::create(
      std::move(timeout_builder).build());
  auto timed_message = request();
  const auto timed = apply_async(timeout_pipeline, timed_message).get();
  const auto timeout_statistics = timeout_pipeline.statistics();
  if (timed.action != rewrite_action::block ||
      timed.failure != STATUS_IO_TIMEOUT ||
      timeout_statistics.timed_out != 1) {
    std::cerr << "timeout action=" << static_cast<int>(timed.action)
              << " failure=" << static_cast<unsigned long>(timed.failure)
              << " count=" << timeout_statistics.timed_out << '\n';
    return failed(2);
  }

  async_transform_policy_builder cancelled_builder({}, options);
  cancelled_builder.requests().transform(
      [](request_message &, const async_policy_context &) {
        return rewrite_result::unchanged();
      });
  auto cancelled_pipeline = async_transform_runtime::create(
      std::move(cancelled_builder).build());
  std::stop_source cancelled;
  cancelled.request_stop();
  auto cancelled_message = request();
  const auto cancelled_outcome =
      apply_async(cancelled_pipeline, cancelled_message,
                  cancelled.get_token()).get();
  if (cancelled_outcome.action != rewrite_action::block ||
      cancelled_outcome.failure != STATUS_CANCELLED ||
      cancelled_pipeline.statistics().cancelled != 1)
    return failed(3);

  async_transform_options overload_options;
  overload_options.maximum_concurrency = 1;
  overload_options.maximum_queue_depth = 1;
  overload_options.timeout = std::chrono::seconds(2);
  async_transform_policy_builder overloaded_builder({}, overload_options);
  std::promise<void> entered;
  auto entered_future = entered.get_future();
  std::atomic_bool release = false;
  std::atomic_bool first = true;
  overloaded_builder.requests().transform(
      [&](request_message &, const async_policy_context &context) {
        if (first.exchange(false))
          entered.set_value();
        while (!release.load(std::memory_order_acquire) &&
               !context.cancellation_requested())
          std::this_thread::yield();
        return rewrite_result::unchanged();
      });
  auto overloaded_pipeline = async_transform_runtime::create(
      std::move(overloaded_builder).build());
  auto first_message = request();
  auto second_message = request();
  auto third_message = request();
  auto first_task = apply_async(overloaded_pipeline, first_message);
  if (entered_future.wait_for(std::chrono::seconds(1)) !=
      std::future_status::ready)
    return failed(4);
  auto second_task = apply_async(overloaded_pipeline, second_message);
  auto third_task = apply_async(overloaded_pipeline, third_message);
  const auto third = third_task.get();
  release.store(true, std::memory_order_release);
  const auto first_outcome = first_task.get();
  const auto second_outcome = second_task.get();
  const auto overload_statistics = overloaded_pipeline.statistics();
  const bool valid = third.action == rewrite_action::block &&
         third.failure == STATUS_DEVICE_BUSY &&
         first_outcome.action == rewrite_action::forward &&
         second_outcome.action == rewrite_action::forward &&
         overload_statistics.overloaded == 1;
  if (!valid) {
    std::cerr << "third-failure="
              << static_cast<unsigned long>(third.failure)
              << " overloads=" << overload_statistics.overloaded
              << " first=" << static_cast<int>(first_outcome.action)
              << " second=" << static_cast<int>(second_outcome.action)
              << '\n';
    return failed(5);
  }

  async_transform_policy_builder callback_builder(transform_limits{}, options);
  std::unique_ptr<async_transform_runtime> callback_pipeline;
  callback_builder.requests().transform(
      [&callback_pipeline](request_message &message,
                           const async_policy_context &) {
        message.headers.set("x-owner-lifetime", "safe");
        callback_pipeline.reset();
        return rewrite_result::headers_changed();
      });
  callback_pipeline = std::make_unique<async_transform_runtime>(
      async_transform_runtime::create(std::move(callback_builder).build()));
  auto *const callback_facade = callback_pipeline.get();
  const auto callback_closed =
      apply_async_owned(*callback_facade, request()).get();
  if (callback_pipeline ||
      callback_closed.outcome.action != rewrite_action::block ||
      callback_closed.outcome.failure != STATUS_CANCELLED)
    return failed(6);

  async_transform_policy_builder raced_builder({}, options);
  std::promise<void> race_entered;
  auto race_entered_future = race_entered.get_future();
  std::atomic_bool race_release = false;
  raced_builder.requests().transform(
      [&race_entered, &race_release](request_message &,
                                    const async_policy_context &context) {
        race_entered.set_value();
        while (!race_release.load(std::memory_order_acquire) &&
               !context.cancellation_requested())
          std::this_thread::yield();
        return rewrite_result::unchanged();
      });
  auto raced_pipeline = async_transform_runtime::create(
      std::move(raced_builder).build());
  auto raced_task = apply_async_owned(raced_pipeline, request());
  if (race_entered_future.wait_for(std::chrono::seconds(1)) !=
      std::future_status::ready)
    return failed(7);
  std::thread closer([&raced_pipeline] {
    raced_pipeline.close();
    raced_pipeline.close();
  });
  const auto raced = raced_task.get();
  race_release.store(true, std::memory_order_release);
  closer.join();
  if (raced.outcome.action != rewrite_action::block ||
      raced.outcome.failure != STATUS_CANCELLED)
    return failed(8);
  try {
    auto rejected = raced_pipeline.apply(request());
    (void)rejected;
    return failed(9);
  } catch (const std::logic_error &) {
  }
  return true;
}

bool test_bounded_message_memory() {
  using namespace ntl::net;
  using namespace ntl::net::http;

  borrowed_bounded_memory_resource counted(
      *std::pmr::new_delete_resource(),
      {.maximum_allocated_bytes = 1024,
       .maximum_single_allocation = 256});
  bool single_allocation_rejected = false;
  {
    request_message message(message_memory_ref{&counted});
    try {
      message.body.resize(257);
    } catch (const std::bad_alloc &) {
      single_allocation_rejected = true;
    }
  }
  if (!single_allocation_rejected || counted.allocated_bytes() != 0) {
    std::cerr << "bounded-memory: single-allocation/release\n";
    return false;
  }

  inspection::content_decoder_registry decoders;
  inspection::content_encoder_registry encoders;
  inspection::register_standard_content_encoders(encoders);
  const auto plain = bytes(std::string(2048, 'A'));
  header_collection coding_headers;
  coding_headers.append("content-encoding", "gzip");
  auto compressed = encode_body(
      coding_headers, plain, "gzip",
      transformed_body_coding::preserve, encoders);
  if (!compressed) {
    std::cerr << "bounded-memory: encode fixture\n";
    return false;
  }
  decoders.add("gzip", [] {
    return std::make_unique<inspection::content_decoder_adapter<
        inspection::zlib_content_decoder>>(
        inspection::zlib_stream_format::gzip,
        inspection::codec_memory_limits{
            .maximum_allocated_bytes = 1,
            .maximum_single_allocation = 1});
  });
  auto codec_failure = decode_body(
      coding_headers, *compressed, decoders,
      {.maximum_encoded_body_bytes = 4096,
       .maximum_decoded_body_bytes = 4096});
  if (codec_failure ||
      static_cast<NTSTATUS>(codec_failure.status()) !=
          STATUS_INSUFFICIENT_RESOURCES) {
    std::cerr << "bounded-memory: codec status="
              << (codec_failure ? 0 : static_cast<unsigned long>(
                     static_cast<NTSTATUS>(codec_failure.status()))) << '\n';
    return false;
  }

  const std::string wire =
      "POST /inspect HTTP/1.1\r\nHost: example.test\r\n"
      "Content-Length: 512\r\n\r\n" +
      std::string(512, 'x');
  const auto wire_bytes = std::as_bytes(std::span(wire));
  transform_pipeline pipeline;
  inspection::content_decoder_registry identity_decoders;
  inspection::content_encoder_registry identity_encoders;

  std::array<std::byte, 256> exhausted_storage{};
  borrowed_fixed_workspace_resource exhausted(exhausted_storage);
  auto exhausted_result = transform_http1_request(
      wire_bytes, pipeline, identity_decoders, identity_encoders,
      {.origin_scheme = "https"},
      message_memory_ref{exhausted.resource()});
  if (exhausted_result ||
      static_cast<NTSTATUS>(exhausted_result.status()) !=
          STATUS_INSUFFICIENT_RESOURCES) {
    std::cerr << "bounded-memory: exhausted status="
              << (exhausted_result ? 0 : static_cast<unsigned long>(
                     static_cast<NTSTATUS>(exhausted_result.status()))) << '\n';
    return false;
  }

  const std::array<ntl::net::http2::header_field, 5> h2_fields{{
      {":method", "POST", false},
      {":scheme", "https", false},
      {":authority", "example.test", false},
      {":path", "/inspect", false},
      {"content-length", "512", false},
  }};
  std::array<std::byte, 256> h2_exhausted_storage{};
  borrowed_fixed_workspace_resource h2_exhausted(h2_exhausted_storage);
  auto h2_exhausted_result = ntl::net::http2::transform_request(
      1, h2_fields, std::as_bytes(std::span(wire)).last(512), {},
      pipeline, identity_decoders, identity_encoders,
      ntl::net::http2::default_maximum_frame_size, true,
      message_memory_ref{h2_exhausted.resource()});
  if (h2_exhausted_result ||
      static_cast<NTSTATUS>(h2_exhausted_result.status()) !=
          STATUS_INSUFFICIENT_RESOURCES) {
    std::cerr << "bounded-memory: HTTP/2 exhausted status="
              << (h2_exhausted_result
                      ? 0
                      : static_cast<unsigned long>(static_cast<NTSTATUS>(
                            h2_exhausted_result.status())))
              << '\n';
    return false;
  }

  const std::string h3_authority(512, 'a');
  const std::array<ntl::net::http3::header_field, 4> h3_fields{{
      {":method", "GET", false},
      {":scheme", "https", false},
      {":authority", h3_authority, false},
      {":path", "/inspect", false},
  }};
  std::array<std::byte, 256> h3_exhausted_storage{};
  borrowed_fixed_workspace_resource h3_exhausted(h3_exhausted_storage);
  auto h3_exhausted_result =
      ntl::net::http3::stream_transform_detail::parse_request_head(
          h3_fields,
          {.maximum_header_bytes = 4096},
          message_memory_ref{h3_exhausted.resource()});
  if (h3_exhausted_result ||
      static_cast<NTSTATUS>(h3_exhausted_result.status()) !=
          STATUS_INSUFFICIENT_RESOURCES) {
    std::cerr << "bounded-memory: HTTP/3 exhausted status="
              << (h3_exhausted_result
                      ? 0
                      : static_cast<unsigned long>(static_cast<NTSTATUS>(
                            h3_exhausted_result.status())))
              << '\n';
    return false;
  }

  std::array<std::byte, 4096> storage{};
  borrowed_fixed_workspace_resource workspace(storage);
  auto transformed = transform_http1_request(
      wire_bytes, pipeline, identity_decoders, identity_encoders,
      {.origin_scheme = "https"},
      message_memory_ref{workspace.resource()});
  const bool valid = transformed &&
         transformed->message.resource() == workspace.resource() &&
         transformed->wire.get_allocator().resource() == workspace.resource();
  if (!valid)
    std::cerr << "bounded-memory: successful binding\n";
  return valid;
}

bool test_selector_owner_first_lifetime() {
  auto request_policy = [] {
    ntl::net::http::transform_pipeline owner;
    auto selector = owner.requests();
    return std::pair{owner, std::move(selector)};
  }();
  request_policy.second.transform(
      [](ntl::net::http::request_message &request) {
        request.headers.set("x-owner-first", "safe");
        return ntl::net::http::rewrite_result::headers_changed();
      });

  ntl::net::http::request_message request;
  request.method = "GET";
  request.authority = "example.test";
  const auto request_result = request_policy.first.apply(request);
  if (request_result.action != ntl::net::http::rewrite_action::forward ||
      request.headers.first("x-owner-first") != "safe")
    return false;

  auto response_policy = [] {
    ntl::net::http::transform_pipeline owner;
    auto selector = owner.responses();
    return std::pair{owner, std::move(selector)};
  }();
  response_policy.second.transform(
      [](const ntl::net::http::request_message &,
         ntl::net::http::response_message &response) {
        response.headers.set("x-owner-first", "safe");
        return ntl::net::http::rewrite_result::headers_changed();
      });

  ntl::net::http::response_message response;
  response.status = 200;
  const auto response_result =
      response_policy.first.apply(request, response);
  return response_result.action ==
             ntl::net::http::rewrite_action::forward &&
         response.headers.first("x-owner-first") == "safe";
}

bool test_stream_policy_owner_first_lifetime() {
  auto opened = [] {
    ntl::net::http::stream_transform_pipeline owner;
    auto selector = owner.chunks();
    selector.transform(
        [](const ntl::net::http::stream_message_context_view &context,
           const ntl::net::http::stream_chunk_view &chunk) {
          if (!context.request || context.request->path != "/lifetime")
            return ntl::net::http::stream_rewrite_result::block();
          return ntl::net::http::stream_rewrite_result::replace(
              std::vector<std::byte>(chunk.bytes.begin(), chunk.bytes.end()));
        });
    ntl::net::http::request_message request;
    request.method = "POST";
    request.authority = "example.test";
    request.path = "/lifetime";
    return owner.open(request);
  }();
  if (!opened)
    return false;
  const std::array payload{std::byte{'o'}, std::byte{'k'}};
  const auto outcome = opened->consume(payload, true);
  return outcome.action ==
             ntl::net::http::stream_rewrite_action::forward &&
         outcome.failure == STATUS_SUCCESS && outcome.final &&
         outcome.bytes == std::vector<std::byte>(payload.begin(), payload.end());
}

bool test_http2_goaway_graceful_shutdown_boundary() {
  ntl::net::http2::exchange_store exchanges;
  auto retained = request();
  if (!exchanges.remember(1, retained).is_ok() || exchanges.empty())
    return false;
  exchanges.erase(1);
  if (!exchanges.empty())
    return false;

  auto pipeline =
      std::make_shared<ntl::net::http::transform_pipeline>();
  auto decoders = std::make_shared<
      ntl::net::inspection::content_decoder_registry>();
  auto encoders = std::make_shared<
      ntl::net::inspection::content_encoder_registry>();
  ntl::net::http2::proxy_connection connection(
      pipeline, decoders, encoders,
      {.require_first_settings = false});
  if (connection.ready_for_graceful_shutdown(
          ntl::net::http2::connection_direction::responses))
    return false;
  const std::array<std::byte, 8> payload{};
  auto wire = ntl::net::http2::transform_detail::make_frame(
      ntl::net::http2::frame_type::goaway, 0, 0, payload);
  if (!wire)
    return false;
  auto frame = ntl::net::http2::frame_view::parse(
      ntl::net::scatter_view::from_contiguous(wire->wire),
      {1024 * 1024, false});
  if (!frame)
    return false;
  auto consumed = connection.consume(
      ntl::net::http2::connection_direction::responses, *frame);
  return consumed && connection.ready_for_graceful_shutdown(
                         ntl::net::http2::connection_direction::responses) &&
         !connection.ready_for_graceful_shutdown(
             ntl::net::http2::connection_direction::requests);
}

bool test_http2_local_settings_ack_boundary() {
  auto pipeline =
      std::make_shared<ntl::net::http::transform_pipeline>();
  auto decoders = std::make_shared<
      ntl::net::inspection::content_decoder_registry>();
  auto encoders = std::make_shared<
      ntl::net::inspection::content_encoder_registry>();
  ntl::net::http2::proxy_connection connection(
      pipeline, decoders, encoders,
      {.require_first_settings = false});
  if (!connection.accept_client_preface(
          ntl::net::http2::client_connection_preface).is_ok() ||
      !connection.expect_local_downstream_settings_ack().is_ok() ||
      connection.expect_local_downstream_settings_ack() !=
          STATUS_INVALID_DEVICE_STATE)
    return false;
  auto wire = ntl::net::http2::transform_detail::make_frame(
      ntl::net::http2::frame_type::settings, 0x01u, 0, {});
  if (!wire)
    return false;
  auto frame = ntl::net::http2::frame_view::parse(
      ntl::net::scatter_view::from_contiguous(wire->wire),
      {1024 * 1024, false});
  if (!frame)
    return false;
  auto local = connection.consume(
      ntl::net::http2::connection_direction::requests, *frame);
  if (!local || !local->consumed || local->forward_original ||
      !local->forward.empty())
    return false;
  auto origin = connection.consume(
      ntl::net::http2::connection_direction::requests, *frame);
  return origin && origin->forward_original;
}

bool test_http2_send_window_owner_first_close() {
  auto owner = std::make_unique<ntl::net::http2::send_window>(0, 0);
  auto waiting = wait_for_send_window_owner_close(*owner);
  owner.reset();
  return waiting.get();
}

bool test_http1_to_h2_h3_forwarding_headers() {
  using namespace ntl::net::http;

  response_message response;
  response.wire_protocol = protocol::http1;
  response.status = 200;
  response.headers.append("Connection", " close, X-Hop ");
  response.headers.append("X-Hop", "remove-me");
  response.headers.append("Keep-Alive", "timeout=5");
  response.headers.append("Proxy-Connection", "keep-alive");
  response.headers.append("Transfer-Encoding", "chunked");
  response.headers.append("Upgrade", "example");
  response.headers.append("TE", "trailers, trailers");
  response.headers.append("X-End-To-End", "preserve-me");
  response.trailers.push_back({"x-hop", "remove-me", false});
  response.trailers.push_back({"x-end-trailer", "preserve-me", false});
  const ntl::status adapted =
      adapt_forwarded_message(response, protocol::http3);
  if (!adapted.is_ok() || response.wire_protocol != protocol::http3 ||
      response.headers.contains("connection") ||
      response.headers.contains("x-hop") ||
      response.headers.contains("keep-alive") ||
      response.headers.contains("proxy-connection") ||
      response.headers.contains("transfer-encoding") ||
      response.headers.contains("upgrade") ||
      response.headers.first("te") != "trailers" ||
      response.headers.first("x-end-to-end") != "preserve-me" ||
      std::any_of(
          response.trailers.begin(), response.trailers.end(),
          [](const header_field &field) { return field.name == "x-hop"; }) ||
      !std::any_of(
          response.trailers.begin(), response.trailers.end(),
          [](const header_field &field) {
            return field.name == "x-end-trailer";
          }))
    return false;

  request_message request;
  request.wire_protocol = protocol::http1;
  request.method = "GET";
  request.authority = "example.test";
  request.path = "/";
  request.headers.append("Host", "example.test");
  request.headers.append("Connection", "X-Request-Hop");
  request.headers.append("X-Request-Hop", "remove-me");
  if (!adapt_forwarded_message(request, protocol::http2).is_ok() ||
      request.headers.contains("host") ||
      request.headers.contains("connection") ||
      request.headers.contains("x-request-hop"))
    return false;

  response_message malformed;
  malformed.wire_protocol = protocol::http1;
  malformed.headers.append("connection", "close,");
  return adapt_forwarded_message(malformed, protocol::http3) ==
         STATUS_DATA_ERROR;
}

} // namespace

int main() {
  const bool pipeline = test_pipeline();
  const bool content = test_content_reencoding();
  const bool terminal = test_terminal_results();
  const bool failure = test_failure_policy();
  const bool protocol_safety =
      test_protocol_safety_and_bodyless_responses();
  const bool http1 = test_http1_backend();
  const bool http1_target =
      test_http1_request_target_and_transfer_coding();
  const bool http2 = test_http2_backend();
  const bool http2_connection =
      test_http2_connection_transformer();
  const bool http2_extended_connect =
      test_http2_extended_connect_transformer();
  const bool stream = test_stream_transform();
  const bool stateful_stream = test_stateful_stream_transform();
  const bool http1_streaming =
      test_http1_streaming_message_transformer();
  const bool http2_streaming =
      test_http2_streaming_connection_transformer();
  const bool http3_streaming =
      test_http3_streaming_connection_transformer();
  const bool compressed_stream = test_compressed_stream_transform();
  const bool asynchronous = test_async_transform();
  const bool bounded_memory = test_bounded_message_memory();
  const bool selector_owner_first =
      test_selector_owner_first_lifetime();
  const bool stream_owner_first =
      test_stream_policy_owner_first_lifetime();
  const bool http2_goaway_shutdown =
      test_http2_goaway_graceful_shutdown_boundary();
  const bool http2_local_settings_ack =
      test_http2_local_settings_ack_boundary();
  const bool http2_send_window_owner_first =
      test_http2_send_window_owner_first_close();
  const bool forwarding_headers =
      test_http1_to_h2_h3_forwarding_headers();
  if (!pipeline || !content || !terminal || !failure ||
      !protocol_safety || !http1 || !http1_target || !http2 ||
       !http2_connection || !http2_extended_connect ||
       !stream || !stateful_stream ||
       !http1_streaming ||
       !http2_streaming || !http3_streaming ||
       !compressed_stream || !asynchronous || !bounded_memory ||
       !selector_owner_first || !stream_owner_first ||
        !http2_goaway_shutdown || !http2_local_settings_ack ||
        !http2_send_window_owner_first || !forwarding_headers) {
    std::cerr << "pipeline=" << pipeline
              << " content=" << content
              << " terminal=" << terminal
              << " failure=" << failure
              << " protocol-safety=" << protocol_safety
              << " http1=" << http1
              << " http1-target=" << http1_target
              << " http2=" << http2
              << " http2-connection=" << http2_connection
              << " http2-extended-connect="
              << http2_extended_connect
              << " stream=" << stream
              << " stateful-stream=" << stateful_stream
              << " http1-streaming=" << http1_streaming
              << " http2-streaming=" << http2_streaming
              << " http3-streaming=" << http3_streaming
              << " compressed-stream=" << compressed_stream
              << " async=" << asynchronous
              << " bounded-memory=" << bounded_memory
              << " selector-owner-first=" << selector_owner_first
              << " stream-owner-first=" << stream_owner_first
              << " http2-goaway-shutdown=" << http2_goaway_shutdown
              << " http2-local-settings-ack="
              << http2_local_settings_ack
              << " http2-send-window-owner-first="
              << http2_send_window_owner_first
              << " forwarding-headers=" << forwarding_headers
              << '\n';
    std::cerr << "NTL HTTP transform contracts failed\n";
    return 1;
  }
  std::cout << "NTL HTTP transform contracts passed\n";
  return 0;
}
