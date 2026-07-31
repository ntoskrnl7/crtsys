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
#include <ntl/net/http/http1_stream_transform>
#include <ntl/net/http/async_transform>
#include <ntl/net/http/stream_transform>
#include <ntl/net/http/transform>
#include <ntl/net/http2/transform>
#include <ntl/net/http2/stream_transform>
#include <ntl/net/http3/stream_transform>
#include <ntl/net/inspection/standard_content_decoders>
#include <ntl/net/inspection/standard_content_encoders>

namespace {

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
    ntl::net::http::async_transform_pipeline &pipeline,
    ntl::net::http::request_message &message,
    std::stop_token cancellation = {}) {
  co_return co_await pipeline.apply(message, cancellation);
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
          request_wire, pipeline, decoders, encoders);
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

  ntl::net::http2::exchange_store exchanges;
  ntl::net::http2::connection_transformer requests(
      ntl::net::http2::connection_direction::requests,
      exchanges, pipeline, decoders, encoders, 7);
  ntl::net::http2::connection_transformer responses(
      ntl::net::http2::connection_direction::responses,
      exchanges, pipeline, decoders, encoders, 11);

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
  ntl::net::http2::exchange_store blocked_exchanges;
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

bool test_stream_transform() {
  using namespace ntl::net::http;
  stream_transform_pipeline pipeline;
  pipeline.chunks().transform(
      [](const stream_message_context &, const stream_chunk &chunk) {
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
      [](const stream_message_context &) {
        return [expected_offset = std::uint64_t{0}](
                   const stream_message_context &,
                   const stream_chunk &chunk) mutable {
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
      [](const stream_message_context &, const stream_chunk &chunk) {
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
        output, decoders,
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
        output, decoders,
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
  if (!fixed_identity || !fixed_gzip || !fixed_br ||
      !chunked_gzip || !close_br || !truncated_fixed ||
      !truncated_chunked)
    std::cerr << "http1 stream fixed=" << fixed_identity
              << " gzip=" << fixed_gzip
              << " br=" << fixed_br
              << " chunked=" << chunked_gzip
              << " close=" << close_br
              << " truncated-fixed=" << truncated_fixed
              << " truncated-chunked=" << truncated_chunked << '\n';
  return fixed_identity && fixed_gzip && fixed_br &&
         chunked_gzip && close_br && truncated_fixed &&
         truncated_chunked;
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
      [](const stream_message_context &, const stream_chunk &chunk) {
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

    ntl::net::http2::exchange_store exchanges;
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
  ntl::net::http2::exchange_store trailer_exchanges;
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
  ntl::net::http2::exchange_store invalid_exchanges;
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
         !invalid_exchanges.request(5);
}

bool test_http3_streaming_connection_transformer() {
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
      [](const stream_message_context &, const stream_chunk &chunk) {
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

    ntl::net::http3::stream_exchange_store exchanges;
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
    return !exchanges.request(1);
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
          [](const stream_message_context &, const stream_chunk &chunk) {
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
  async_transform_pipeline pipeline({}, options);
  pipeline.requests().transform(
      [](request_message &message, const async_policy_context &context) {
        if (context.cancellation_requested())
          return rewrite_result::block();
        message.headers.set("x-async-policy", "complete");
        return rewrite_result::headers_changed();
      });
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
  async_transform_pipeline timeout_pipeline({}, timeout_options);
  timeout_pipeline.requests().transform(
      [](request_message &, const async_policy_context &context) {
        while (!context.cancellation_requested())
          std::this_thread::sleep_for(std::chrono::milliseconds(1));
        return rewrite_result::unchanged();
      });
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

  async_transform_pipeline cancelled_pipeline({}, options);
  cancelled_pipeline.requests().transform(
      [](request_message &, const async_policy_context &) {
        return rewrite_result::unchanged();
      });
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
  async_transform_pipeline overloaded_pipeline({}, overload_options);
  std::promise<void> entered;
  auto entered_future = entered.get_future();
  std::atomic_bool release = false;
  std::atomic_bool first = true;
  overloaded_pipeline.requests().transform(
      [&](request_message &, const async_policy_context &context) {
        if (first.exchange(false))
          entered.set_value();
        while (!release.load(std::memory_order_acquire) &&
               !context.cancellation_requested())
          std::this_thread::yield();
        return rewrite_result::unchanged();
      });
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
  return true;
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
  const bool http2 = test_http2_backend();
  const bool http2_connection =
      test_http2_connection_transformer();
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
  if (!pipeline || !content || !terminal || !failure ||
      !protocol_safety || !http1 || !http2 ||
       !http2_connection || !stream || !stateful_stream ||
       !http1_streaming ||
       !http2_streaming || !http3_streaming ||
       !compressed_stream || !asynchronous) {
    std::cerr << "pipeline=" << pipeline
              << " content=" << content
              << " terminal=" << terminal
              << " failure=" << failure
              << " protocol-safety=" << protocol_safety
              << " http1=" << http1
              << " http2=" << http2
              << " http2-connection=" << http2_connection
              << " stream=" << stream
              << " stateful-stream=" << stateful_stream
              << " http1-streaming=" << http1_streaming
              << " http2-streaming=" << http2_streaming
              << " http3-streaming=" << http3_streaming
              << " compressed-stream=" << compressed_stream
              << " async=" << asynchronous
              << '\n';
    std::cerr << "NTL HTTP transform contracts failed\n";
    return 1;
  }
  std::cout << "NTL HTTP transform contracts passed\n";
  return 0;
}
