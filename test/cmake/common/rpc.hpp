#pragma once

#include <list>
#include <map>
#include <set>
#include <string>
#include <vector>

#include <ntl/deps/zpp/serializer.h>

class point {
public:
  point() = default;
  point(int x, int y) noexcept : m_x(x), m_y(y) {}

  int get_x() const noexcept { return m_x; }
  int get_y() const noexcept { return m_y; }

  friend zpp::serializer::access;
  template <typename Archive, typename Self>
  static void serialize(Archive &archive, Self &self) {
    archive(self.m_x, self.m_y);
  }

private:
  int m_x = 0;
  int m_y = 0;
};

NTL_RPC_BEGIN(test_rpc)

// The line-based forms remain a compact convenience contract. The explicit-ID
// forms below cover stable externally visible method IDs.
NTL_ADD_CALLBACK_1(test_rpc, int, test_inc, int, value, { return value + 1; })
NTL_ADD_CALLBACK_1(test_rpc, int, test_dec, int, value, { return value - 1; })

NTL_ADD_CALLBACK_ID_2(test_rpc, 0x900, int, test_sum, int, left, int, right, {
  return left + right;
})
NTL_ADD_CALLBACK_ID_2(test_rpc, 0x901, int, test_stable_sum, int, left, int,
                      right, { return left + right; })
NTL_ADD_CALLBACK_ID_3(test_rpc, 0x902, int, test_sum, int, a, int, b, int, c, {
  return a + b + c;
})
NTL_ADD_CALLBACK_ID_4(test_rpc, 0x903, int, test_sum, int, a, int, b, int, c,
                      int, d, { return a + b + c + d; })
NTL_ADD_CALLBACK_ID_5(test_rpc, 0x904, int, test_sum, int, a, int, b, int, c,
                      int, d, int, e, { return a + b + c + d + e; })

NTL_ADD_CALLBACK_ID_0(test_rpc, 0x905, void, test_void, { return; })

NTL_ADD_CALLBACK_ID_1(test_rpc, 0x906, bool, test_vec,
                      const std::vector<int> &, values, {
                        return values.empty();
                      })
NTL_ADD_CALLBACK_ID_1(test_rpc, 0x907, bool, test_set,
                      const std::set<int> &, values, {
                        return values.empty();
                      })

NTL_ADD_CALLBACK_ID_1(
    test_rpc, 0x908,
    NTL_RPC_BOUNDED_RESPONSE(1024 * 1024, std::vector<std::string>), test_list,
    const std::list<int> &, values, {
      std::vector<std::string> result;
      for (auto value : values)
        result.push_back(std::to_string(value));
      return result;
    })

NTL_ADD_CALLBACK_ID_1(
    test_rpc, 0x909,
    NTL_RPC_BOUNDED_RESPONSE(1024 * 1024, std::map<int, std::string>), test_map,
    NTL_RPC_ARG_PACK(const std::map<int, int> &), values, {
      std::map<int, std::string> result;
      for (const auto &value : values)
        result.insert({value.first, std::to_string(value.first)});
      return result;
    })

NTL_ADD_CALLBACK_ID_2(
    test_rpc, 0x90A, bool, test_map2,
    NTL_RPC_ARG_PACK(const std::map<int, int> &), left,
    NTL_RPC_ARG_PACK(const std::map<int, int> &), right, {
      return left.size() > right.size();
    })

NTL_ADD_CALLBACK_ID_2(test_rpc, 0x90B, point, test_point_class,
                      const point &, left, const point &, right, {
                        return left.get_x() > right.get_x() ? left : right;
                      })

NTL_RPC_END(test_rpc)
