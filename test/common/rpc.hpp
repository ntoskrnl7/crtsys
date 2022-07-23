#pragma once

#include <map>
#include <set>
#include <vector>

#include <ntl/deps/zpp/serializer.h>

class point {
public:
  point() = default;
  point(int x, int y) noexcept : m_x(x), m_y(y) {}
  friend zpp::serializer::access;
  template <typename Archive, typename Self>
  static void serialize(Archive &archive, Self &self) {
    archive(self.m_x, self.m_y);
  }
  int get_x() const noexcept { return m_x; }
  int get_y() const noexcept { return m_y; }

private:
  int m_x = 0;
  int m_y = 0;
};

NTL_RPC_BEGIN(test_rpc)

NTL_ADD_CALLBACK_1(test_rpc, int, test_inc, int, i, { return i + 1; })

NTL_ADD_CALLBACK_1(test_rpc, int, test_dec, int, i, { return i - 1; })

NTL_ADD_CALLBACK_2(test_rpc, int, test_sum, int, a, int, b, { return a + b; })

NTL_ADD_CALLBACK_3(test_rpc, int, test_sum, int, a, int, b, int, c,
                   { return a + b + c; })

NTL_ADD_CALLBACK_4(test_rpc, int, test_sum, int, a, int, b, int, c, int, d,
                   { return a + b + c + d; })

NTL_ADD_CALLBACK_5(test_rpc, int, test_sum, int, a, int, b, int, c, int, d, int,
                   e, { return a + b + c + d + e; })

NTL_ADD_CALLBACK_0(test_rpc, void, test_void, { return; })

NTL_ADD_CALLBACK_1(test_rpc, bool, test_vec, const std::vector<int> &, vec,
                   { return vec.empty(); })

NTL_ADD_CALLBACK_1(test_rpc, bool, test_set, const std::set<int> &, set,
                   { return set.empty(); })

NTL_ADD_CALLBACK_1(test_rpc, std::vector<std::string>, test_list,
                   const std::list<int> &, list, {
                     std::vector<std::string> ret;
                     for (auto i : list)
                       ret.push_back(std::to_string(i));
                     return ret;
                   })

NTL_ADD_CALLBACK_1(test_rpc, NTL_RPC_ARG_PACK(std::map<int, std::string>),
                   test_map, NTL_RPC_ARG_PACK(const std::map<int, int> &), map,
                   {
                     std::map<int, std::string> ret;
                     for (auto e : map)
                       ret.insert({e.first, std::to_string(e.first)});
                     return ret;
                     ;
                   })

NTL_ADD_CALLBACK_2(test_rpc, bool, test_map2,
                   NTL_RPC_ARG_PACK(const std::map<int, int> &), map,
                   NTL_RPC_ARG_PACK(const std::map<int, int> &), map2,
                   { return map.size() > map2.size(); })

NTL_ADD_CALLBACK_2(test_rpc, point, test_point_class, const point &, p1,
                   const point &, p2,
                   { return p1.get_x() > p2.get_x() ? p1 : p2; })

NTL_RPC_END(test_rpc)