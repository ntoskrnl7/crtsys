
NTL_ADD_CALLBACK_1(int, test_inc, int, i, {
  i = i;
  return i + 1;
})

NTL_ADD_CALLBACK_1(int, test_dec, int, i, {
  i = i;
  return i - 1;
})

NTL_ADD_CALLBACK_2(int, test_sum, int, a, int, b, { return a + b; })

NTL_ADD_CALLBACK_3(int, test_sum, int, a, int, b, int, c, { return a + b + c; })

NTL_ADD_CALLBACK_4(int, test_sum, int, a, int, b, int, c, int, d,
                   { return a + b + c + d; })

NTL_ADD_CALLBACK_5(int, test_sum, int, a, int, b, int, c, int, d, int, e,
                   { return a + b + c + d + e; })

NTL_ADD_CALLBACK_0(void, test_void, { return; })

NTL_ADD_CALLBACK_1(bool, test_vec, const std::vector<int> &, vec,
                   { return vec.empty(); })

NTL_ADD_CALLBACK_1(bool, test_set, const std::set<int> &, set,
                   { return set.empty(); })

NTL_ADD_CALLBACK_1(std::vector<std::string>, test_list, const std::list<int> &,
                   list, {
                     std::vector<std::string> ret;
                     for (auto i : list)
                       ret.push_back(std::to_string(i));
                     return ret;
                   })

NTL_ADD_CALLBACK_1(NTL_RPC_ARG_PACK(std::map<int, std::string>), test_map,
                   NTL_RPC_ARG_PACK(const std::map<int, int> &), map, {
                     std::map<int, std::string> ret;
                     for (auto e : map)
                       ret.insert({e.first, std::to_string(e.first)});
                     return ret;
                     ;
                   })

NTL_ADD_CALLBACK_2(bool, test_map2,
                   NTL_RPC_ARG_PACK(const std::map<int, int> &), map,
                   NTL_RPC_ARG_PACK(const std::map<int, int> &), map2,
                   { return map.size() > map2.size(); })

NTL_ADD_CALLBACK_2(point, test_point_class, const point &, p1, const point &,
                   p2, { return p1.get_x() > p2.get_x() ? p1 : p2; })
