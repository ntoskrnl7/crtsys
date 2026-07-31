#include <ntl/net/buffer/scatter_view>

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <type_traits>

namespace {

bool expect(bool condition) noexcept { return condition; }

bool test_fragmented_read() noexcept {
  const std::array<std::byte, 1> first{std::byte{0x12}};
  const std::array<std::byte, 2> second{
      std::byte{0x34}, std::byte{0x56}};
  const std::array<std::byte, 3> third{
      std::byte{0x78}, std::byte{0x9a}, std::byte{0xbc}};
  const std::array<std::span<const std::byte>, 3> segments{
      first, second, third};

  const auto bytes = ntl::net::scatter_view::from_segments(segments);
  if (!expect(bytes.size() == 6))
    return false;

  ntl::net::byte_cursor cursor(bytes);
  const auto word = cursor.read_be16();
  const auto dword = cursor.read_be32();
  return expect(word && *word == 0x1234) &&
         expect(dword && *dword == 0x56789abc) &&
         expect(cursor.empty());
}

bool test_subview_and_copy() noexcept {
  const std::array<std::byte, 2> first{
      std::byte{'a'}, std::byte{'b'}};
  const std::array<std::byte, 1> empty{};
  const std::array<std::byte, 3> second{
      std::byte{'c'}, std::byte{'d'}, std::byte{'e'}};
  const std::array<std::span<const std::byte>, 3> segments{
      first, std::span<const std::byte>(empty.data(), 0), second};

  const auto bytes = ntl::net::scatter_view::from_segments(segments);
  const auto middle = bytes.subview(1, 3);
  if (!middle)
    return false;

  std::array<std::byte, 3> copy{};
  if (!middle->copy_to(copy).is_ok())
    return false;
  return expect(copy[0] == std::byte{'b'}) &&
         expect(copy[1] == std::byte{'c'}) &&
         expect(copy[2] == std::byte{'d'}) &&
         expect(!bytes.subview(5, 2));
}

bool test_fragmented_write() noexcept {
  std::array<std::byte, 1> first{};
  std::array<std::byte, 1> second{};
  std::array<std::byte, 2> third{};
  std::array<std::span<std::byte>, 3> segments{first, second, third};

  const auto bytes = ntl::net::mutable_scatter_view::from_segments(segments);
  if (!bytes.write_be32(0, 0x12345678).is_ok())
    return false;

  return expect(first[0] == std::byte{0x12}) &&
         expect(second[0] == std::byte{0x34}) &&
         expect(third[0] == std::byte{0x56}) &&
         expect(third[1] == std::byte{0x78}) &&
         expect(!bytes.write_be16(3, 1).is_ok());
}

bool test_early_stop() noexcept {
  const std::array<std::byte, 2> first{};
  const std::array<std::byte, 2> second{};
  const std::array<std::span<const std::byte>, 2> segments{first, second};
  const auto bytes = ntl::net::scatter_view::from_segments(segments);

  std::size_t visits = 0;
  const auto status = bytes.for_each_chunk(
      [&](std::span<const std::byte>) noexcept {
        ++visits;
        return false;
      });
  return expect(status.is_ok()) && expect(visits == 1);
}

bool test_pattern_across_fragments() noexcept {
  constexpr auto pattern = ntl::net::byte_literal("abab");
  const std::array<std::byte, 3> first{
      std::byte{'x'}, std::byte{'a'}, std::byte{'b'}};
  const std::array<std::byte, 3> second{
      std::byte{'a'}, std::byte{'b'}, std::byte{'a'}};
  const std::array<std::span<const std::byte>, 2> segments{first, second};

  const auto match =
      ntl::net::scan_bytes(ntl::net::scatter_view::from_segments(segments),
                      pattern);
  if (!match || !match->found || match->offset != 1)
    return false;

  const std::array<std::byte, 3> incomplete{
      std::byte{'q'}, std::byte{'a'}, std::byte{'b'}};
  const auto suffix =
      ntl::net::scan_bytes(ntl::net::scatter_view::from_contiguous(incomplete),
                      pattern);
  return expect(suffix && !suffix->found &&
                suffix->trailing_prefix == 2);
}

static_assert(std::is_trivially_copyable_v<ntl::net::scatter_view>);
static_assert(std::is_trivially_copyable_v<ntl::net::mutable_scatter_view>);
static_assert(!std::is_invocable_v<
              decltype(&ntl::net::scatter_view::copy_to),
              ntl::net::scatter_view, std::span<const std::byte>,
              std::size_t>);

} // namespace

int main() {
  return test_fragmented_read() && test_subview_and_copy() &&
                 test_fragmented_write() && test_early_stop() &&
                 test_pattern_across_fragments()
             ? 0
             : 1;
}
