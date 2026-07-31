#include <ntl/net/inspection/standard_content_decoders>
#include <ntl/net/inspection/standard_content_encoders>
#include <ntl/net/inspection/content_stream>

#include <cstddef>
#include <iostream>
#include <span>
#include <string_view>
#include <vector>

namespace {

std::vector<std::byte> bytes(std::string_view value) {
  return {reinterpret_cast<const std::byte *>(value.data()),
          reinterpret_cast<const std::byte *>(value.data() + value.size())};
}

bool round_trip(
    std::string_view coding,
    ntl::net::inspection::content_encoder_registry &encoders,
    ntl::net::inspection::content_decoder_registry &decoders) {
  const auto input = bytes(
      "NuGet automatically links the bounded NTL content codecs.");
  auto encoder = encoders.create(coding);
  auto decoder = decoders.create(coding);
  if (!encoder || !decoder)
    return false;
  auto encoded = ntl::net::inspection::encode_complete(
      *encoder, input, 64 * 1024);
  if (!encoded || encoded->empty())
    return false;
  auto decoded = ntl::net::inspection::decode_complete(
      *decoder,
      ntl::net::scatter_view::from_contiguous(
          std::span<const std::byte>(*encoded)),
      64 * 1024);
  return decoded && *decoded == input;
}

bool incremental_round_trip(
    std::string_view coding,
    ntl::net::inspection::content_encoder_registry &encoders,
    ntl::net::inspection::content_decoder_registry &decoders) {
  std::vector<std::byte> input;
  const auto line = bytes("NuGet incremental NTL content codec validation.\n");
  for (std::size_t index = 0; index != 512; ++index)
    input.insert(input.end(), line.begin(), line.end());

  auto stream = ntl::net::inspection::content_encoding_stream::create(
      decoders, encoders, coding,
      {.maximum_input_chunk_size = 1,
       .maximum_stage_output_chunk_size = 2 * 1024 * 1024,
       .maximum_encoded_stream_size = 2 * 1024 * 1024,
       .maximum_decoded_stream_size = 2 * 1024 * 1024,
       .maximum_transformed_stream_size = 2 * 1024 * 1024,
       .maximum_output_stream_size = 2 * 1024 * 1024,
       .maximum_expansion_ratio = 128,
       .expansion_slack_size = 64 * 1024,
       .maximum_coding_layers = 4});
  if (!stream)
    return false;

  std::vector<std::byte> encoded;
  for (std::size_t index = 0; index != input.size(); ++index) {
    auto chunk = stream->encode(
        std::span<const std::byte>(&input[index], 1),
        index + 1 == input.size());
    if (!chunk)
      return false;
    encoded.insert(encoded.end(), chunk->bytes.begin(), chunk->bytes.end());
  }

  auto decoder = ntl::net::inspection::content_encoding_stream::create(
      decoders, encoders, coding,
      {.maximum_input_chunk_size = 1,
       .maximum_stage_output_chunk_size = 2 * 1024 * 1024,
       .maximum_encoded_stream_size = 2 * 1024 * 1024,
       .maximum_decoded_stream_size = 2 * 1024 * 1024,
       .maximum_transformed_stream_size = 2 * 1024 * 1024,
       .maximum_output_stream_size = 2 * 1024 * 1024,
       .maximum_expansion_ratio = 128,
       .expansion_slack_size = 64 * 1024,
       .maximum_coding_layers = 4});
  if (!decoder)
    return false;
  std::vector<std::byte> decoded;
  for (std::size_t index = 0; index != encoded.size(); ++index) {
    auto chunk = decoder->decode(
        std::span<const std::byte>(&encoded[index], 1),
        index + 1 == encoded.size());
    if (!chunk)
      return false;
    decoded.insert(decoded.end(), chunk->bytes.begin(), chunk->bytes.end());
  }
  return decoded == input && decoder->decode_finished();
}

} // namespace

int main() {
  ntl::net::inspection::content_encoder_registry encoders;
  ntl::net::inspection::content_decoder_registry decoders;
  ntl::net::inspection::register_standard_content_encoders(encoders);
  ntl::net::inspection::register_standard_content_decoders(decoders);
  if (!round_trip("gzip", encoders, decoders) ||
      !round_trip("deflate", encoders, decoders) ||
      !round_trip("br", encoders, decoders) ||
      !incremental_round_trip("gzip", encoders, decoders) ||
      !incremental_round_trip("deflate", encoders, decoders) ||
      !incremental_round_trip("br", encoders, decoders) ||
      !incremental_round_trip("gzip, br", encoders, decoders)) {
    std::cerr << "NTL NuGet content codec consumer failed\n";
    return 1;
  }
  std::cout << "NTL NuGet content codecs passed: one-shot and incremental "
               "gzip, deflate, br, gzip+br\n";
  return 0;
}
