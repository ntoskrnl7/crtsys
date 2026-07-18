#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <list>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <tuple>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <variant>
#include <vector>

#include <ntl/rpc/common>

enum class rpc_wire_mode : std::uint32_t {
  boundary_values = 0x10203040u,
  empty_values = 0x50607080u,
};

struct rpc_architecture_info {
  std::uint32_t pointer_bits = 0;
  std::uint32_t size_bits = 0;
  std::uint32_t difference_bits = 0;
  std::uint32_t wchar_bits = 0;

  friend zpp::serializer::access;
  template <typename Archive, typename Self>
  static void serialize(Archive &archive, Self &self) {
    archive(self.pointer_bits, self.size_bits, self.difference_bits,
            self.wchar_bits);
  }
};

struct rpc_nested_value {
  std::int64_t id = 0;
  std::string name;
  std::optional<std::uint64_t> measurement;

  friend zpp::serializer::access;
  template <typename Archive, typename Self>
  static void serialize(Archive &archive, Self &self) {
    archive(self.id, self.name, self.measurement);
  }

  friend bool operator==(const rpc_nested_value &left,
                         const rpc_nested_value &right) {
    return left.id == right.id && left.name == right.name &&
           left.measurement == right.measurement;
  }
};

struct rpc_cross_bitness_payload {
  std::uint8_t unsigned8 = 0;
  std::int8_t signed8 = 0;
  std::uint16_t unsigned16 = 0;
  std::int16_t signed16 = 0;
  std::uint32_t unsigned32 = 0;
  std::int32_t signed32 = 0;
  std::uint64_t unsigned64 = 0;
  std::int64_t signed64 = 0;
  float float32 = 0;
  double float64 = 0;
  bool boolean = false;
  rpc_wire_mode mode = rpc_wire_mode::empty_values;
  std::string text;
  std::wstring wide_text;
  std::vector<std::uint8_t> bytes;
  std::vector<std::uint32_t> numbers;
  std::vector<std::string> words;
  std::vector<std::vector<std::uint16_t>> matrix;
  std::vector<rpc_nested_value> nested;
  std::array<std::uint64_t, 3> fixed{};
  std::list<std::int32_t> list_values;
  std::deque<std::string> deque_values;
  std::set<std::int64_t> set_values;
  std::multiset<std::int32_t> multiset_values;
  std::unordered_set<std::uint64_t> unordered_set_values;
  std::map<std::int32_t, std::string> ordered_values;
  std::multimap<std::int32_t, std::string> multimap_values;
  std::unordered_map<std::uint32_t, std::wstring> unordered_values;
  std::pair<std::int64_t, std::uint64_t> pair_value{};
  std::tuple<std::uint8_t, std::uint16_t, std::uint32_t, std::uint64_t>
      tuple_value{};
  std::optional<std::string> optional_value;
  std::optional<std::string> empty_optional;
  std::variant<std::int64_t, std::string> variant_number{std::int64_t{0}};
  std::variant<std::int64_t, std::string> variant_text{std::int64_t{0}};
  std::uint64_t portable_size = 0;
  std::int64_t portable_difference = 0;

  friend zpp::serializer::access;
  template <typename Archive, typename Self>
  static void serialize(Archive &archive, Self &self) {
    archive(self.unsigned8, self.signed8, self.unsigned16, self.signed16,
            self.unsigned32, self.signed32, self.unsigned64, self.signed64,
            self.float32, self.float64, self.boolean, self.mode, self.text,
            self.wide_text, self.bytes, self.numbers, self.words, self.matrix,
            self.nested, self.fixed, self.list_values, self.deque_values,
            self.set_values, self.multiset_values, self.unordered_set_values,
            self.ordered_values, self.multimap_values, self.unordered_values,
            self.pair_value, self.tuple_value, self.optional_value,
            self.empty_optional, self.variant_number, self.variant_text,
            self.portable_size, self.portable_difference);
  }

  friend bool operator==(const rpc_cross_bitness_payload &left,
                         const rpc_cross_bitness_payload &right) {
    return left.unsigned8 == right.unsigned8 &&
           left.signed8 == right.signed8 &&
           left.unsigned16 == right.unsigned16 &&
           left.signed16 == right.signed16 &&
           left.unsigned32 == right.unsigned32 &&
           left.signed32 == right.signed32 &&
           left.unsigned64 == right.unsigned64 &&
           left.signed64 == right.signed64 &&
           left.float32 == right.float32 && left.float64 == right.float64 &&
           left.boolean == right.boolean && left.mode == right.mode &&
           left.text == right.text && left.wide_text == right.wide_text &&
           left.bytes == right.bytes && left.numbers == right.numbers &&
           left.words == right.words && left.matrix == right.matrix &&
           left.nested == right.nested &&
           left.fixed == right.fixed &&
           left.list_values == right.list_values &&
           left.deque_values == right.deque_values &&
           left.set_values == right.set_values &&
           left.multiset_values == right.multiset_values &&
           left.unordered_set_values == right.unordered_set_values &&
           left.ordered_values == right.ordered_values &&
           left.multimap_values == right.multimap_values &&
           left.unordered_values == right.unordered_values &&
           left.pair_value == right.pair_value &&
           left.tuple_value == right.tuple_value &&
           left.optional_value == right.optional_value &&
           left.empty_optional == right.empty_optional &&
           left.variant_number == right.variant_number &&
           left.variant_text == right.variant_text &&
           left.portable_size == right.portable_size &&
           left.portable_difference == right.portable_difference;
  }
};

namespace crtsys_rpc_cross_bitness {

constexpr auto architecture =
    ntl::rpc::method<0xA01, rpc_architecture_info()>{};
constexpr auto echo =
    ntl::rpc::method<0xA02,
                     rpc_cross_bitness_payload(
                         const rpc_cross_bitness_payload &)>{}
        .max_response_size<2 * 1024 * 1024>();
constexpr auto narrow_reply =
    ntl::rpc::method<0xA03, std::uint32_t()>{};
constexpr auto vector_size =
    ntl::rpc::method<0xA04,
                     std::uint32_t(const std::vector<std::uint8_t> &)>{};
constexpr auto echo_count = ntl::rpc::method<0xA05, std::uint32_t()>{};

} // namespace crtsys_rpc_cross_bitness
