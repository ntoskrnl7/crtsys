#include <gtest/gtest.h>
#include <windows.h>

#include <ntl/rpc/client>
// rpc client stub code
#include "common/rpc.hpp"

TEST(ntl_rpc_client, invoke_callback_by_invoke_method) {
  ntl::rpc::client cli(L"test_rpc", 1024 * 1024);

  using namespace test_rpc;
  EXPECT_EQ(cli.invoke<int>(test_inc_1_index, 1), 2);
  EXPECT_EQ(cli.invoke<int>(test_dec_1_index, 1), 0);

  EXPECT_EQ(cli.invoke<int>(test_sum_2_index, 1, 2), 3);
  EXPECT_EQ(cli.invoke<int>(test_sum_3_index, 1, 2, 3), 6);
  EXPECT_EQ(cli.invoke<int>(test_sum_4_index, 1, 2, 3, 4), 10);
  EXPECT_EQ(cli.invoke<int>(test_sum_5_index, 1, 2, 3, 4, 5), 15);

  EXPECT_NO_FATAL_FAILURE(cli.invoke<void>(test_void_0_index));

  std::vector<int> vec = {1, 2, 3};
  EXPECT_FALSE(cli.invoke<bool>(test_vec_1_index, vec));

  std::set<int> set = {1, 2, 3, 4};
  EXPECT_FALSE(cli.invoke<bool>(test_set_1_index, set));

  auto test_list_ret = test_list({1, 2, 3, 4, 5, 6});
  for (size_t i = 0; i < test_list_ret.size(); i++)
    EXPECT_STREQ(std::to_string(i + 1).c_str(), test_list_ret[i].c_str());

  auto test_map_ret = test_map({{1, 1}, {2, 2}, {3, 3}});
  EXPECT_FALSE(test_map_ret.empty());
  for (auto e : test_map_ret)
    EXPECT_STREQ(std::to_string(e.first).c_str(), e.second.c_str());

  EXPECT_TRUE(test_map2({{1, 1}, {2, 2}, {3, 3}}, {}));

  auto test_point_class_ret = test_point_class(point(1, 1), point(4, 1));
  EXPECT_EQ(test_point_class_ret.get_x(), 4);
  EXPECT_EQ(test_point_class_ret.get_y(), 1);
}

TEST(ntl_rpc_client, invoke_callback_by_symbol) {
  using namespace test_rpc;

  EXPECT_EQ(test_inc(1), 2);
  EXPECT_EQ(test_dec(1), 0);

  EXPECT_EQ(test_sum(1, 2), 3);
  EXPECT_EQ(test_sum(1, 2, 3), 6);
  EXPECT_EQ(test_sum(1, 2, 3, 4), 10);
  EXPECT_EQ(test_sum(1, 2, 3, 4, 5), 15);

  EXPECT_NO_FATAL_FAILURE(test_void());

  EXPECT_FALSE(test_vec({1, 2, 3}));

  EXPECT_FALSE(test_set({1, 2, 3, 4}));

  auto test_list_ret = test_list({1, 2, 3, 4, 5, 6});
  for (size_t i = 0; i < test_list_ret.size(); i++)
    EXPECT_STREQ(std::to_string(i + 1).c_str(), test_list_ret[i].c_str());

  auto test_map_ret = test_map({{1, 1}, {2, 2}, {3, 3}});
  EXPECT_FALSE(test_map_ret.empty());
  for (auto e : test_map_ret)
    EXPECT_STREQ(std::to_string(e.first).c_str(), e.second.c_str());

  EXPECT_TRUE(test_map2({{1, 1}, {2, 2}, {3, 3}}, {}));

  auto test_point_class_ret = test_point_class(point(1, 1), point(4, 1));
  EXPECT_EQ(test_point_class_ret.get_x(), 4);
  EXPECT_EQ(test_point_class_ret.get_y(), 1);
}

TEST(ntl_device, device_io_control) {
  HANDLE hDevice = CreateFileW(
      L"\\\\?\\Global\\GLOBALROOT\\Device\\test_device",
      GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);
  EXPECT_NE(hDevice, INVALID_HANDLE_VALUE);

  if (hDevice != INVALID_HANDLE_VALUE) {
    DWORD bytes_returned;
    char buffer[sizeof("world")];

    EXPECT_TRUE(DeviceIoControl(
        hDevice,
        CTL_CODE(FILE_DEVICE_NTL_RPC, 0, METHOD_BUFFERED, FILE_ANY_ACCESS),
        "hello", 5, buffer, sizeof("world"), &bytes_returned, NULL));

    EXPECT_EQ(bytes_returned, sizeof("world"));
    EXPECT_STREQ(buffer, "world");

    CloseHandle(hDevice);
  }
}

int main() {
  testing::InitGoogleTest();
  return RUN_ALL_TESTS();
}
