#include <ntl/net/inspection/content_decoder>
#include <ntl/net/framing>
#include <ntl/net/inspection/core>

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <type_traits>

namespace {

bool expect(bool condition) noexcept { return condition; }

bool test_content_across_fragments() noexcept {
  const std::array<std::byte, 4> first{
      std::byte{'a'}, std::byte{'B'}, std::byte{'L'}, std::byte{'O'}};
  const std::array<std::byte, 5> second{
      std::byte{'C'}, std::byte{'K'}, std::byte{'M'}, std::byte{'E'},
      std::byte{'z'}};
  const std::array<std::span<const std::byte>, 2> segments{first, second};
  const ntl::net::inspection::content_view content(
      ntl::net::scatter_view::from_segments(segments));
  const auto contains = content.contains("BLOCKME");
  const auto starts = content.starts_with("aBLO");
  return contains && *contains && starts && *starts;
}

bool test_length_prefix() noexcept {
  const std::array<std::byte, 3> prefix{
      std::byte{0}, std::byte{0}, std::byte{0}};
  const std::array<std::byte, 6> body{
      std::byte{5}, std::byte{'h'}, std::byte{'e'}, std::byte{'l'},
      std::byte{'l'}, std::byte{'o'}};
  const std::array<std::span<const std::byte>, 2> segments{prefix, body};
  auto bytes = ntl::net::scatter_view::from_segments(segments);
  ntl::net::framing::u32_be_length_prefix framer(64);
  const auto result = ntl::net::framing::probe(
      framer, bytes, {.maximum_frame_size = 68});
  return result.state() == ntl::net::framing::probe_state::complete &&
         result.frame_size() == 9 &&
         result.content_offset() == 4 &&
         result.content_size() == 5;
}

class flag_length_framer {
public:
  ntl::net::framing::frame_probe
  probe(ntl::net::scatter_view bytes) const noexcept {
    if (!bytes || bytes.size() < 1)
      return ntl::net::framing::frame_probe::need_more(1);
    const auto flags = bytes.read<std::uint8_t>();
    if (!flags)
      return ntl::net::framing::frame_probe::malformed();
    const std::size_t length_size = (*flags & 1) != 0 ? 4 : 2;
    const std::size_t header_size = 1 + length_size;
    if (bytes.size() < header_size)
      return ntl::net::framing::frame_probe::need_more(header_size);

    auto header = bytes.subview(1, length_size);
    if (!header)
      return ntl::net::framing::frame_probe::malformed();
    ntl::net::byte_cursor cursor(*header);
    std::uint32_t payload = 0;
    if (length_size == 2) {
      const auto value = cursor.read_be16();
      if (!value)
        return ntl::net::framing::frame_probe::malformed();
      payload = *value;
    } else {
      const auto value = cursor.read_be32();
      if (!value)
        return ntl::net::framing::frame_probe::malformed();
      payload = *value;
    }
    const std::size_t total = header_size + payload;
    if (bytes.size() < total)
      return ntl::net::framing::frame_probe::need_more(total);
    return ntl::net::framing::frame_probe::complete(
        total, header_size, payload);
  }
};

bool test_custom_dynamic_framer() noexcept {
  const std::array<std::byte, 8> bytes{
      std::byte{0}, std::byte{0}, std::byte{5}, std::byte{'h'},
      std::byte{'e'}, std::byte{'l'}, std::byte{'l'}, std::byte{'o'}};
  flag_length_framer framer;
  const auto result = ntl::net::framing::probe(
      framer, ntl::net::scatter_view::from_contiguous(bytes),
      {.maximum_frame_size = 64});
  return result.state() == ntl::net::framing::probe_state::complete &&
         result.frame_size() == bytes.size() &&
         result.content_offset() == 3 &&
         result.content_size() == 5;
}

bool test_delimiter_and_limits() noexcept {
  constexpr auto delimiter = ntl::net::byte_literal("\r\n\r\n");
  ntl::net::framing::delimiter_framer framer(delimiter, 16);
  const std::array<std::byte, 7> incomplete{
      std::byte{'a'}, std::byte{'b'}, std::byte{'c'}, std::byte{'\r'},
      std::byte{'\n'}, std::byte{'\r'}, std::byte{'x'}};
  const auto need = ntl::net::framing::probe(
      framer, ntl::net::scatter_view::from_contiguous(incomplete),
      {.maximum_frame_size = 16});
  if (need.state() != ntl::net::framing::probe_state::need_more)
    return false;

  const std::array<std::byte, 9> complete{
      std::byte{'a'}, std::byte{'b'}, std::byte{'c'}, std::byte{'\r'},
      std::byte{'\n'}, std::byte{'\r'}, std::byte{'\n'}, std::byte{'x'},
      std::byte{'y'}};
  const auto found = ntl::net::framing::probe(
      framer, ntl::net::scatter_view::from_contiguous(complete),
      {.maximum_frame_size = 16});
  return found.state() == ntl::net::framing::probe_state::complete &&
         found.frame_size() == 7 && found.content_size() == 3;
}

bool test_decoder_contract() noexcept {
  const std::array<std::byte, 5> input{
      std::byte{'h'}, std::byte{'e'}, std::byte{'l'}, std::byte{'l'},
      std::byte{'o'}};
  ntl::net::inspection::identity_content_decoder decoder;
  auto decoded = ntl::net::inspection::decode_complete(
      decoder, ntl::net::scatter_view::from_contiguous(input), 5);
  if (!decoded || decoded->size() != input.size())
    return false;
  auto rejected = ntl::net::inspection::decode_complete(
      decoder, ntl::net::scatter_view::from_contiguous(input), 4);
  return !rejected &&
         static_cast<NTSTATUS>(rejected.status()) ==
             STATUS_BUFFER_OVERFLOW;
}

static_assert(std::is_trivially_copyable_v<
              ntl::net::inspection::content_view>);
static_assert(std::is_same_v<
              decltype(ntl::net::inspection::evaluate(
                  [](const ntl::net::inspection::content_view &) {
                    return ntl::net::inspection::verdict::permit;
                  },
                  ntl::net::inspection::content_view{})),
              ntl::net::inspection::verdict>);

} // namespace

int main() {
  return test_content_across_fragments() &&
                 test_length_prefix() &&
                 test_custom_dynamic_framer() &&
                 test_delimiter_and_limits() &&
                 test_decoder_contract()
             ? 0
             : 1;
}
