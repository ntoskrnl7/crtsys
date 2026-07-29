#include "http3_inspection.hpp"

#include <charconv>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#include <ntl/net/http3/backend>

#include "http1_support.hpp"

namespace crtsys::wfp_sample::browser_https {
namespace {

constexpr std::size_t maximum_tracked_streams = 256;
constexpr std::size_t maximum_buffered_bodies =
    16 * 1024 * 1024;
constexpr std::size_t maximum_http3_frame_payload =
    1024 * 1024;
constexpr std::size_t maximum_decoded_header_size =
    256 * 1024;

bool lower_case_name(std::string_view name) noexcept {
  if (name.empty())
    return false;
  for (const unsigned char character : name) {
    if (character >= 'A' && character <= 'Z')
      return false;
  }
  return true;
}

bool valid_regular_header(
    const ntl::net::http3::header_field &field) noexcept {
  if (!lower_case_name(field.name) ||
      field.name.front() == ':')
    return false;
  if (field.name == "connection" ||
      field.name == "proxy-connection" ||
      field.name == "keep-alive" ||
      field.name == "transfer-encoding" ||
      field.name == "upgrade")
    return false;
  return field.name != "te" ||
         ascii_equal_ci(
             trim_http_ows(field.value), "trailers");
}

bool parse_content_length(
    std::string_view value,
    std::optional<std::size_t> &current) noexcept {
  std::size_t parsed = 0;
  const auto converted = std::from_chars(
      value.data(), value.data() + value.size(), parsed);
  if (converted.ec != std::errc{} ||
      converted.ptr != value.data() + value.size() ||
      (current && *current != parsed))
    return false;
  current = parsed;
  return true;
}

class browser_http3_sink
    : public ntl::net::http3::inspection_sink {
public:
  virtual void reset() noexcept = 0;
  virtual unsigned last_status() const noexcept {
    return 0;
  }
  virtual std::optional<std::filesystem::path>
  html_path() const {
    return std::nullopt;
  }
};

class http3_request_sink final
    : public browser_http3_sink {
public:
  ntl::status on_headers(
      std::uint64_t stream_id,
      std::span<const ntl::net::http3::header_field>
          fields) noexcept override {
    try {
      const auto found = streams_.find(stream_id);
      if (found != streams_.end()) {
        if (found->second.trailers)
          return STATUS_DATA_ERROR;
        for (const auto &field : fields) {
          if (!valid_regular_header(field))
            return STATUS_DATA_ERROR;
        }
        found->second.trailers = true;
        return ntl::status::ok();
      }
      if (streams_.size() >= maximum_tracked_streams)
        return STATUS_QUOTA_EXCEEDED;

      bool regular_seen = false;
      bool method_seen = false;
      bool scheme_seen = false;
      bool authority_seen = false;
      bool path_seen = false;
      bool connect = false;
      std::optional<std::size_t> content_length;
      for (const auto &field : fields) {
        if (!lower_case_name(field.name))
          return STATUS_DATA_ERROR;
        const bool pseudo = field.name.front() == ':';
        if (pseudo && regular_seen)
          return STATUS_DATA_ERROR;
        regular_seen = regular_seen || !pseudo;
        if (!pseudo) {
          if (!valid_regular_header(field))
            return STATUS_DATA_ERROR;
          if (field.name == "content-length" &&
              !parse_content_length(
                  field.value, content_length))
            return STATUS_DATA_ERROR;
          continue;
        }

        if (field.name == ":method" && !method_seen &&
            !field.value.empty()) {
          method_seen = true;
          connect = field.value == "CONNECT";
        } else if (
            field.name == ":scheme" && !scheme_seen &&
            field.value == "https") {
          scheme_seen = true;
        } else if (
            field.name == ":authority" &&
            !authority_seen && !field.value.empty()) {
          authority_seen = true;
        } else if (
            field.name == ":path" && !path_seen &&
            !field.value.empty()) {
          path_seen = true;
        } else {
          // Extended CONNECT (:protocol) has its own product path.
          return STATUS_NOT_SUPPORTED;
        }
      }
      if (!method_seen || !authority_seen ||
          (connect ? (scheme_seen || path_seen)
                   : (!scheme_seen || !path_seen)))
        return STATUS_DATA_ERROR;

      streams_.emplace(
          stream_id,
          request_state{content_length, 0, false});
      return ntl::status::ok();
    } catch (...) {
      return STATUS_INSUFFICIENT_RESOURCES;
    }
  }

  ntl::status on_data(
      std::uint64_t stream_id,
      ntl::net::scatter_view data) noexcept override {
    const auto found = streams_.find(stream_id);
    if (found == streams_.end() ||
        found->second.trailers)
      return STATUS_DATA_ERROR;
    auto &stream = found->second;
    if (data.size() >
        maximum_http_body_size - stream.received)
      return STATUS_BUFFER_OVERFLOW;
    stream.received += data.size();
    return ntl::status::ok();
  }

  ntl::status on_stream_end(
      std::uint64_t stream_id) noexcept override {
    const auto found = streams_.find(stream_id);
    if (found == streams_.end())
      return STATUS_DATA_ERROR;
    if (found->second.content_length &&
        *found->second.content_length !=
            found->second.received)
      return STATUS_DATA_ERROR;
    streams_.erase(found);
    return ntl::status::ok();
  }

  void reset() noexcept override {
    streams_.clear();
  }

private:
  struct request_state {
    std::optional<std::size_t> content_length;
    std::size_t received = 0;
    bool trailers = false;
  };

  std::unordered_map<std::uint64_t, request_state>
      streams_;
};

class http3_response_sink final
    : public browser_http3_sink {
public:
  http3_response_sink(
      std::wstring server_name,
      const ntl::net::inspection::content_decoder_registry
          &decoders,
      browser_html_logger &logger) noexcept
      : server_name_(std::move(server_name)),
        decoders_(&decoders), logger_(&logger) {}

  ntl::status on_headers(
      std::uint64_t stream_id,
      std::span<const ntl::net::http3::header_field>
          fields) noexcept override {
    try {
      auto found = streams_.find(stream_id);
      if (found == streams_.end()) {
        if (streams_.size() >= maximum_tracked_streams)
          return STATUS_QUOTA_EXCEEDED;
        found = streams_.try_emplace(stream_id).first;
      }
      auto &stream = found->second;
      if (stream.final_headers) {
        if (stream.trailers)
          return STATUS_DATA_ERROR;
        for (const auto &field : fields) {
          if (!valid_regular_header(field))
            return STATUS_DATA_ERROR;
        }
        stream.trailers = true;
        return ntl::status::ok();
      }

      bool regular_seen = false;
      bool status_seen = false;
      unsigned status = 0;
      std::string content_type;
      std::string content_encoding;
      std::optional<std::size_t> content_length;
      for (const auto &field : fields) {
        if (!lower_case_name(field.name))
          return STATUS_DATA_ERROR;
        const bool pseudo = field.name.front() == ':';
        if (pseudo && regular_seen)
          return STATUS_DATA_ERROR;
        regular_seen = regular_seen || !pseudo;
        if (pseudo) {
          if (field.name != ":status" || status_seen ||
              field.value.size() != 3)
            return STATUS_DATA_ERROR;
          const auto converted = std::from_chars(
              field.value.data(),
              field.value.data() + field.value.size(),
              status);
          if (converted.ec != std::errc{} ||
              converted.ptr !=
                  field.value.data() + field.value.size() ||
              status < 100 || status > 999)
            return STATUS_DATA_ERROR;
          status_seen = true;
          continue;
        }
        if (!valid_regular_header(field))
          return STATUS_DATA_ERROR;
        if (field.name == "content-type")
          content_type = field.value;
        else if (field.name == "content-encoding") {
          if (!content_encoding.empty())
            content_encoding.append(", ");
          content_encoding.append(field.value);
        } else if (
            field.name == "content-length" &&
            !parse_content_length(
                field.value, content_length)) {
          return STATUS_DATA_ERROR;
        }
      }
      if (!status_seen)
        return STATUS_DATA_ERROR;
      if (status >= 100 && status < 200)
        return ntl::status::ok();

      stream.status = status;
      stream.content_type = std::move(content_type);
      stream.content_encoding =
          std::move(content_encoding);
      stream.content_length = content_length;
      stream.final_headers = true;
      return ntl::status::ok();
    } catch (...) {
      return STATUS_INSUFFICIENT_RESOURCES;
    }
  }

  ntl::status on_data(
      std::uint64_t stream_id,
      ntl::net::scatter_view data) noexcept override {
    try {
      const auto found = streams_.find(stream_id);
      if (found == streams_.end() ||
          !found->second.final_headers ||
          found->second.trailers)
        return STATUS_DATA_ERROR;
      auto &body = found->second.body;
      if (data.size() >
          maximum_http_body_size - body.size()) {
        record_limit(
            "encoded body", stream_id, body.size(),
            data.size(), maximum_http_body_size);
        return STATUS_BUFFER_OVERFLOW;
      }
      if (data.size() >
          maximum_buffered_bodies -
              buffered_body_bytes_) {
        record_limit(
            "connection body quota", stream_id,
            buffered_body_bytes_, data.size(),
            maximum_buffered_bodies);
        return STATUS_QUOTA_EXCEEDED;
      }
      const auto copied = data.for_each_chunk(
          [&body](
              std::span<const std::byte> chunk) noexcept {
            try {
              body.insert(
                  body.end(), chunk.begin(), chunk.end());
              return true;
            } catch (...) {
              return false;
            }
          });
      if (!copied.is_ok())
        return STATUS_INSUFFICIENT_RESOURCES;
      buffered_body_bytes_ += data.size();
      return ntl::status::ok();
    } catch (...) {
      return STATUS_INSUFFICIENT_RESOURCES;
    }
  }

  ntl::status on_stream_end(
      std::uint64_t stream_id) noexcept override {
    return complete(stream_id);
  }

  void reset() noexcept override {
    streams_.clear();
    buffered_body_bytes_ = 0;
    last_status_ = 0;
    html_path_.reset();
  }

  unsigned last_status() const noexcept override {
    return last_status_;
  }

  std::optional<std::filesystem::path>
  html_path() const override {
    return html_path_;
  }

private:
  struct stream_state {
    unsigned status = 0;
    std::string content_type;
    std::string content_encoding;
    std::optional<std::size_t> content_length;
    std::vector<std::byte> body;
    bool final_headers = false;
    bool trailers = false;
  };

  void record_limit(
      std::string_view kind, std::uint64_t stream_id,
      std::size_t retained, std::size_t incoming,
      std::size_t maximum) noexcept {
    try {
      logger_->record_error(
          "HTTP/3 " + std::string(kind) + " limit host=" +
          narrow_dns_name(server_name_) +
          " stream=" + std::to_string(stream_id) +
          " retained=" + std::to_string(retained) +
          " incoming=" + std::to_string(incoming) +
          " maximum=" + std::to_string(maximum));
    } catch (...) {
    }
  }

  ntl::status complete(
      std::uint64_t stream_id) noexcept {
    try {
      const auto found = streams_.find(stream_id);
      if (found == streams_.end() ||
          !found->second.final_headers)
        return STATUS_DATA_ERROR;
      auto &stream = found->second;
      if (stream.content_length &&
          *stream.content_length != stream.body.size())
        return STATUS_DATA_ERROR;
      const std::size_t buffered_size =
          stream.body.size();
      std::vector<std::byte> decoded_body;
      if (!stream.body.empty() ||
          (!stream.content_encoding.empty() &&
           !ascii_equal_ci(
               trim_http_ows(stream.content_encoding),
               "identity"))) {
        auto decoded =
            ntl::net::inspection::decode_content_encoding(
                *decoders_,
                ntl::net::scatter_view::from_contiguous(
                    std::span<const std::byte>(
                        stream.body)),
                stream.content_encoding,
                {.maximum_encoded_size =
                     maximum_http_body_size,
                 .maximum_decoded_size =
                     maximum_http_body_size,
                 .maximum_expansion_ratio = 64,
                 .maximum_coding_layers = 4});
        if (!decoded) {
          logger_->record_error(
              "HTTP/3 content decode failed host=" +
              narrow_dns_name(server_name_) +
              " stream=" +
              std::to_string(stream_id) +
              " encoding=" +
              (stream.content_encoding.empty()
                   ? std::string("identity")
                   : stream.content_encoding) +
              " status=" +
              std::to_string(
                  static_cast<std::uint32_t>(
                      static_cast<NTSTATUS>(
                          decoded.status()))));
          return decoded.status();
        }
        decoded_body = std::move(*decoded);
      }

      parsed_http_response response;
      response.status = stream.status;
      response.content_type =
          std::move(stream.content_type);
      response.content_encoding =
          std::move(stream.content_encoding);
      response.body = std::move(decoded_body);
      response.wire_size = buffered_size;
      response.body_decoded = true;
      last_status_ = response.status;
      auto logged =
          logger_->record_response(server_name_, response);
      if (logged && !html_path_)
        html_path_ = std::move(logged);
      streams_.erase(found);
      buffered_body_bytes_ -= buffered_size;
      return ntl::status::ok();
    } catch (...) {
      return STATUS_INSUFFICIENT_RESOURCES;
    }
  }

  std::wstring server_name_;
  const ntl::net::inspection::content_decoder_registry *decoders_;
  browser_html_logger *logger_;
  std::unordered_map<std::uint64_t, stream_state>
      streams_;
  std::size_t buffered_body_bytes_ = 0;
  unsigned last_status_ = 0;
  std::optional<std::filesystem::path> html_path_;
};

} // namespace

class browser_http3_inspector::implementation {
public:
  implementation(
      http3_inspection_direction direction,
      ntl::net::http3::qpack_decoder &qpack,
      std::wstring server_name,
      const ntl::net::inspection::content_decoder_registry
          &decoders,
      browser_html_logger &logger)
      : sink_(
            direction ==
                    http3_inspection_direction::
                        browser_to_origin
                ? std::unique_ptr<browser_http3_sink>(
                      std::make_unique<
                          http3_request_sink>())
                : std::unique_ptr<browser_http3_sink>(
                      std::make_unique<
                          http3_response_sink>(
                          std::move(server_name),
                          decoders, logger))),
        inspector_(
            qpack,
            {.maximum_concurrent_request_streams =
                 maximum_tracked_streams,
             .maximum_buffered_bytes_per_stream =
                 maximum_http3_frame_payload + 16,
             .frames = {
                 maximum_http3_frame_payload}},
            maximum_decoded_header_size) {}

  ntl::status consume(
      std::uint64_t stream_id,
      ntl::net::scatter_view plaintext,
      bool final) noexcept {
    return inspector_.consume_request_stream(
        stream_id, plaintext, final, *sink_);
  }

  void reset() noexcept {
    inspector_.reset();
    sink_->reset();
  }

  unsigned last_status() const noexcept {
    return sink_->last_status();
  }

  std::optional<std::filesystem::path>
  html_path() const {
    return sink_->html_path();
  }

private:
  std::unique_ptr<browser_http3_sink> sink_;
  ntl::net::http3::connection_inspector inspector_;
};

browser_http3_inspector::browser_http3_inspector(
    http3_inspection_direction direction,
    ntl::net::http3::qpack_decoder &qpack,
    std::wstring server_name,
    const ntl::net::inspection::content_decoder_registry
        &decoders,
    browser_html_logger &logger)
    : implementation_(
          std::make_unique<implementation>(
              direction, qpack, std::move(server_name),
              decoders, logger)) {}

browser_http3_inspector::~browser_http3_inspector() =
    default;

ntl::status browser_http3_inspector::consume_stream(
    std::uint64_t stream_id,
    ntl::net::scatter_view plaintext,
    bool final) noexcept {
  return implementation_->consume(
      stream_id, plaintext, final);
}

void browser_http3_inspector::reset() noexcept {
  implementation_->reset();
}

unsigned
browser_http3_inspector::last_status() const noexcept {
  return implementation_->last_status();
}

std::optional<std::filesystem::path>
browser_http3_inspector::html_path() const {
  return implementation_->html_path();
}

} // namespace crtsys::wfp_sample::browser_https
