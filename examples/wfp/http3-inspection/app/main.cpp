#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <brotli/encode.h>

#include <ntl/net/inspection/content_decoder>
#include <ntl/net/http3/backend>
#include <ntl/net/http3/qpack>
#include <ntl/net/inspection/standard_content_decoders>

namespace {

constexpr std::size_t maximum_wire_size = 1024 * 1024;
constexpr std::size_t maximum_html_size = 4 * 1024 * 1024;

std::vector<std::byte>
brotli_encode(std::span<const std::byte> input) {
  std::vector<std::byte> output(
      ::BrotliEncoderMaxCompressedSize(input.size()));
  std::size_t output_size = output.size();
  if (::BrotliEncoderCompress(
          BROTLI_DEFAULT_QUALITY, BROTLI_DEFAULT_WINDOW,
          BROTLI_MODE_TEXT, input.size(),
          reinterpret_cast<const std::uint8_t *>(
              input.data()),
          &output_size,
          reinterpret_cast<std::uint8_t *>(
              output.data())) == BROTLI_FALSE)
    return {};
  output.resize(output_size);
  return output;
}

bool append_quic_varint(
    std::vector<std::byte> &output,
    std::uint64_t value) {
  if (value <= 63) {
    output.push_back(static_cast<std::byte>(value));
    return true;
  }
  if (value <= 16383) {
    output.push_back(static_cast<std::byte>(
        0x40u | ((value >> 8) & 0x3fu)));
    output.push_back(
        static_cast<std::byte>(value & 0xffu));
    return true;
  }
  return false;
}

std::vector<std::byte>
make_http3_response(std::span<const std::byte> body) {
  // Zero-dynamic-table QPACK:
  //   :status: 200              static index 25
  //   content-encoding: br      static index 42
  //   content-type: text/html   static index 52
  constexpr std::array<std::byte, 5> header_block{
      std::byte{0x00}, std::byte{0x00},
      std::byte{0xd9}, std::byte{0xea},
      std::byte{0xf4}};

  std::vector<std::byte> wire;
  wire.reserve(header_block.size() + body.size() + 8);
  wire.push_back(std::byte{0x01});
  if (!append_quic_varint(
          wire, header_block.size()))
    return {};
  wire.insert(
      wire.end(), header_block.begin(),
      header_block.end());
  wire.push_back(std::byte{0x00});
  if (!append_quic_varint(wire, body.size()))
    return {};
  wire.insert(wire.end(), body.begin(), body.end());
  return wire;
}

class html_response_sink final
    : public ntl::net::http3::inspection_sink {
public:
  ntl::status on_headers(
      std::uint64_t,
      std::span<const ntl::net::http3::header_field>
          fields) noexcept override {
    try {
      if (headers_seen_)
        return STATUS_DATA_ERROR;
      headers_seen_ = true;
      for (const auto &field : fields) {
        if (field.name == ":status")
          status_ = field.value;
        else if (field.name == "content-type")
          content_type_ = field.value;
        else if (field.name == "content-encoding")
          content_encoding_ = field.value;
      }
      return ntl::status::ok();
    } catch (...) {
      return STATUS_INSUFFICIENT_RESOURCES;
    }
  }

  ntl::status on_data(
      std::uint64_t,
      ntl::net::scatter_view data) noexcept override {
    if (data.size() >
        maximum_wire_size - encoded_body_.size())
      return STATUS_BUFFER_OVERFLOW;
    const auto copied = data.for_each_chunk(
        [this](
            std::span<const std::byte> chunk) noexcept {
          try {
            encoded_body_.insert(
                encoded_body_.end(),
                chunk.begin(), chunk.end());
            return true;
          } catch (...) {
            return false;
          }
        });
    return copied.is_ok()
               ? ntl::status::ok()
               : ntl::status(STATUS_INSUFFICIENT_RESOURCES);
  }

  ntl::result<std::vector<std::byte>>
  decode(
      const ntl::net::inspection::content_decoder_registry
          &decoders) const noexcept {
    if (!headers_seen_ || status_ != "200" ||
        content_type_.find("text/html") != 0)
      return ntl::unexpected(STATUS_DATA_ERROR);
    return ntl::net::inspection::decode_content_encoding(
        decoders,
        ntl::net::scatter_view::from_contiguous(
            std::span<const std::byte>(encoded_body_)),
        content_encoding_,
        {.maximum_encoded_size = maximum_wire_size,
         .maximum_decoded_size = maximum_html_size,
         .maximum_expansion_ratio = 128,
         .maximum_coding_layers = 4});
  }

private:
  bool headers_seen_ = false;
  std::string status_;
  std::string content_type_;
  std::string content_encoding_;
  std::vector<std::byte> encoded_body_;
};

class inspector_backend_sink final
    : public ntl::net::http3::quic_backend_sink {
public:
  inspector_backend_sink(
      ntl::net::http3::connection_inspector &inspector,
      ntl::net::http3::inspection_sink &sink) noexcept
      : inspector_(&inspector), sink_(&sink) {}

  ntl::status on_connected(
      std::string_view negotiated_alpn) noexcept override {
    if (negotiated_alpn != "h3")
      return STATUS_PROTOCOL_NOT_SUPPORTED;
    connected_ = true;
    return ntl::status::ok();
  }

  ntl::status on_request_stream(
      std::uint64_t stream_id,
      ntl::net::scatter_view plaintext,
      bool final) noexcept override {
    if (!connected_)
      return STATUS_INVALID_DEVICE_STATE;
    return inspector_->consume_request_stream(
        stream_id, plaintext, final, *sink_);
  }

  ntl::status on_qpack_encoder_stream(
      ntl::net::scatter_view) noexcept override {
    // This example advertises a zero-sized dynamic QPACK table.
    return STATUS_NOT_SUPPORTED;
  }

  void on_closed(NTSTATUS status) noexcept override {
    closed_ = true;
    close_status_ = status;
  }

  bool cleanly_closed() const noexcept {
    return closed_ && NT_SUCCESS(close_status_);
  }

private:
  ntl::net::http3::connection_inspector *inspector_;
  ntl::net::http3::inspection_sink *sink_;
  bool connected_ = false;
  bool closed_ = false;
  NTSTATUS close_status_ = STATUS_UNSUCCESSFUL;
};

class replay_quic_backend final
    : public ntl::net::http3::quic_transport_backend {
public:
  explicit replay_quic_backend(
      std::vector<std::byte> stream)
      : stream_(std::move(stream)) {}

  ntl::net::http3::quic_backend_capabilities
  capabilities() const noexcept override {
    return {
        .available = true,
        .tls13_termination = true,
        .destination_redirection = false,
        .qpack_dynamic_table = false,
        .encrypted_client_hello = false,
        .arbitrary_browser_server_identity = false};
  }

  ntl::status run(
      ntl::net::http3::quic_backend_sink &sink) noexcept override {
    const auto connected = sink.on_connected("h3");
    if (!connected.is_ok())
      return connected;

    // Deliberately split both frame headers and payloads. A QUIC
    // callback is not an HTTP message boundary.
    constexpr std::array<std::size_t, 4> chunk_sizes{
        1, 3, 7, 11};
    std::size_t offset = 0;
    std::size_t chunk_index = 0;
    while (offset != stream_.size()) {
      const std::size_t count = (std::min)(
          chunk_sizes[chunk_index % chunk_sizes.size()],
          stream_.size() - offset);
      const bool final = offset + count == stream_.size();
      const auto view = ntl::net::scatter_view::from_contiguous(
          std::span<const std::byte>(stream_)
              .subspan(offset, count));
      const auto consumed =
          sink.on_request_stream(0, view, final);
      if (!consumed.is_ok()) {
        sink.on_closed(
            static_cast<NTSTATUS>(consumed));
        return consumed;
      }
      offset += count;
      ++chunk_index;
    }
    sink.on_closed(STATUS_SUCCESS);
    return ntl::status::ok();
  }

  ntl::status write_stream(
      std::uint64_t, ntl::net::scatter_view,
      bool) noexcept override {
    return STATUS_NOT_SUPPORTED;
  }

  void stop() noexcept override {}

private:
  std::vector<std::byte> stream_;
};

} // namespace

int main() {
  const std::string html =
      "<!doctype html><html><body>"
      "bounded HTTP/3 Brotli inspection"
      "</body></html>";
  const auto plain = std::as_bytes(std::span(html));
  const auto compressed = brotli_encode(plain);
  const auto wire = make_http3_response(compressed);
  if (compressed.empty() || wire.empty()) {
    std::cerr << "failed to create deterministic input\n";
    return 1;
  }

  ntl::net::http3::qpack_decoder_adapter<
      ntl::net::http3::bounded_static_qpack_decoder>
      qpack;
  ntl::net::http3::connection_inspector inspector(
      qpack,
      {.maximum_concurrent_request_streams = 8,
       .maximum_buffered_bytes_per_stream =
           maximum_wire_size,
       .frames = {maximum_wire_size}},
      64 * 1024);
  html_response_sink response;
  inspector_backend_sink adapter(inspector, response);
  replay_quic_backend backend(wire);

  const auto capabilities = backend.capabilities();
  if (!capabilities.available ||
      !capabilities.tls13_termination ||
      capabilities.qpack_dynamic_table ||
      capabilities.ready_for_transparent_browser()) {
    std::cerr << "unexpected backend capabilities\n";
    return 2;
  }
  const auto inspected = backend.run(adapter);
  if (!inspected.is_ok() || !adapter.cleanly_closed()) {
    std::cerr << "HTTP/3 stream inspection failed: 0x"
              << std::hex
              << static_cast<unsigned long>(
                     static_cast<NTSTATUS>(inspected))
              << '\n';
    return 3;
  }

  ntl::net::inspection::content_decoder_registry decoders;
  ntl::net::inspection::register_standard_content_decoders(
      decoders);
  const auto decoded = response.decode(decoders);
  if (!decoded ||
      *decoded != std::vector<std::byte>(
                      plain.begin(), plain.end())) {
    std::cerr << "decoded HTML did not match\n";
    return 4;
  }

  std::cout
      << "NTL HTTP/3 inspection ok: split QUIC stream, "
         "static QPACK, Brotli HTML, bounded decode\n";
  return 0;
}
