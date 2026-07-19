#include <gtest/gtest.h>
#include <windows.h>

#include <chrono>
#include <cstddef>
#include <system_error>

#include <ntl/rpc/client>
// rpc client stub code
#include "common/rpc.hpp"

extern "C" std::size_t ntl_rpc_cxx14_async_operation_size() noexcept {
  return sizeof(ntl::rpc::detail::async_operation);
}

TEST(ntl_rpc_client, invoke_callback_by_invoke_method) {
  ntl::rpc::client cli(L"test_rpc");

  using namespace test_rpc;
  ntl::rpc::contract_requirements requirements;
  requirements.contract_version(rpc_contract_version)
      .capabilities(rpc_capabilities)
      .method(test_inc_1_method)
      .method(test_stable_sum_2_method)
      .method(test_list_1_method)
      .method(test_cooperative_delay_2_method);
  const auto contract = cli.require_contract(requirements);
  EXPECT_EQ(contract.contract_version(), rpc_contract_version);
  EXPECT_TRUE(contract.supports(test_stable_sum_2_method));

  EXPECT_EQ(cli.invoke(test_inc_1_method, 1), 2);
  EXPECT_EQ(cli.invoke(test_dec_1_method, 1), 0);

  EXPECT_EQ(cli.invoke(test_sum_2_method, 1, 2), 3);
  EXPECT_EQ(cli.invoke(test_stable_sum_2_method, 1, 2), 3);
  EXPECT_EQ(cli.invoke(test_sum_3_method, 1, 2, 3), 6);
  EXPECT_EQ(cli.invoke(test_sum_4_method, 1, 2, 3, 4), 10);
  EXPECT_EQ(cli.invoke(test_sum_5_method, 1, 2, 3, 4, 5), 15);

  EXPECT_NO_FATAL_FAILURE(cli.invoke(test_void_0_method));

  std::vector<int> vec = {1, 2, 3};
  EXPECT_FALSE(cli.invoke(test_vec_1_method, vec));

  std::set<int> set = {1, 2, 3, 4};
  EXPECT_FALSE(cli.invoke(test_set_1_method, set));

  auto test_list_ret =
      cli.invoke(test_list_1_method, std::list<int>{1, 2, 3, 4, 5, 6});
  for (size_t i = 0; i < test_list_ret.size(); i++)
    EXPECT_STREQ(std::to_string(i + 1).c_str(), test_list_ret[i].c_str());

  auto test_map_ret = cli.invoke(
      test_map_1_method, std::map<int, int>{{1, 1}, {2, 2}, {3, 3}});
  EXPECT_FALSE(test_map_ret.empty());
  for (auto e : test_map_ret)
    EXPECT_STREQ(std::to_string(e.first).c_str(), e.second.c_str());

  EXPECT_TRUE(cli.invoke(test_map2_2_method,
                         std::map<int, int>{{1, 1}, {2, 2}, {3, 3}},
                         std::map<int, int>{}));

  auto test_point_class_ret =
      cli.invoke(test_point_class_2_method, point(1, 1), point(4, 1));
  EXPECT_EQ(test_point_class_ret.get_x(), 4);
  EXPECT_EQ(test_point_class_ret.get_y(), 1);

  EXPECT_EQ(cli.invoke(test_cooperative_delay_2_method, std::uint32_t{0},
                       std::uint32_t{42}),
            42u);

  auto cancelled = cli.invoke_async(test_cooperative_delay_2_method,
                                    std::uint32_t{2000}, std::uint32_t{43});
  EXPECT_EQ(cancelled.wait_for(std::chrono::milliseconds(50)),
            ntl::rpc::async_wait_status::timeout);
  EXPECT_TRUE(cancelled.cancel());
  EXPECT_THROW((void)cancelled.get(), std::system_error);
}

TEST(ntl_rpc_client, invoke_generated_wrapper) {
  using namespace test_rpc;

  EXPECT_EQ(test_inc(1), 2);
  EXPECT_EQ(test_dec(1), 0);
  EXPECT_EQ(test_sum(1, 2), 3);
  EXPECT_EQ(test_stable_sum(1, 2), 3);
  EXPECT_EQ(test_sum(1, 2, 3), 6);
  EXPECT_EQ(test_sum(1, 2, 3, 4), 10);
  EXPECT_EQ(test_sum(1, 2, 3, 4, 5), 15);
  EXPECT_NO_FATAL_FAILURE(test_void());
  EXPECT_FALSE(test_vec(std::vector<int>{1, 2, 3}));
  EXPECT_FALSE(test_set(std::set<int>{1, 2, 3, 4}));

  const auto strings = test_list(std::list<int>{1, 2, 3});
  ASSERT_EQ(strings.size(), 3u);
  EXPECT_EQ(strings[0], "1");
  EXPECT_EQ(strings[2], "3");

  const auto mapped =
      test_map(std::map<int, int>{{1, 1}, {2, 2}, {3, 3}});
  EXPECT_EQ(mapped.at(2), "2");
  EXPECT_TRUE(test_map2(std::map<int, int>{{1, 1}},
                        std::map<int, int>{}));

  const auto selected = test_point_class(point(1, 1), point(4, 1));
  EXPECT_EQ(selected.get_x(), 4);
  EXPECT_EQ(selected.get_y(), 1);
  EXPECT_EQ(test_cooperative_delay(std::uint32_t{0}, std::uint32_t{44}), 44u);
}

TEST(ntl_rpc_client, invoke_generated_async_wrapper) {
  using namespace test_rpc;

  EXPECT_EQ(test_inc_async(1).get(), 2);
  EXPECT_EQ(test_sum_async(1, 2).get(), 3);
  EXPECT_EQ(test_sum_async(1, 2, 3).get(), 6);
  EXPECT_EQ(test_sum_async(1, 2, 3, 4).get(), 10);
  EXPECT_EQ(test_sum_async(1, 2, 3, 4, 5).get(), 15);
  EXPECT_NO_FATAL_FAILURE(test_void_async().get());

  auto cancelled = test_cooperative_delay_async(
      std::uint32_t{2000}, std::uint32_t{45});
  EXPECT_EQ(cancelled.wait_for(std::chrono::milliseconds(50)),
            ntl::rpc::async_wait_status::timeout);
  EXPECT_TRUE(cancelled.cancel());
  EXPECT_THROW((void)cancelled.get(), std::system_error);
}

#include "common/test_device.h"

TEST(ntl_device, device_io_control) {
  HANDLE hDevice = CreateFileW(
      L"\\\\?\\Global\\GLOBALROOT\\Device\\" TEST_DEVICE_NAME,
      GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);
  EXPECT_NE(hDevice, INVALID_HANDLE_VALUE);

  if (hDevice != INVALID_HANDLE_VALUE) {
    DWORD bytes_returned;
    char buffer[sizeof("world")];

    EXPECT_TRUE(DeviceIoControl(hDevice, TEST_DEVICE_CTL, "hello", 5, buffer,
                                sizeof("world"), &bytes_returned, NULL));

    EXPECT_EQ(bytes_returned, sizeof("world"));
    EXPECT_STREQ(buffer, "world");

    test_device_state state = {};
    EXPECT_TRUE(DeviceIoControl(hDevice, TEST_DEVICE_STATE_CTL, NULL, 0, &state,
                                sizeof(state), &bytes_returned, NULL));
    EXPECT_EQ(bytes_returned, sizeof(state));

    int create_count = state.create_count;
    int close_count = state.close_count;
    EXPECT_GT(create_count, close_count);

    EXPECT_TRUE(CloseHandle(hDevice));

    hDevice = CreateFileW(
        L"\\\\?\\Global\\GLOBALROOT\\Device\\" TEST_DEVICE_NAME,
        GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);
    EXPECT_NE(hDevice, INVALID_HANDLE_VALUE);

    if (hDevice != INVALID_HANDLE_VALUE) {
      state = {};
      EXPECT_TRUE(DeviceIoControl(hDevice, TEST_DEVICE_STATE_CTL, NULL, 0,
                                  &state, sizeof(state), &bytes_returned,
                                  NULL));
      EXPECT_EQ(bytes_returned, sizeof(state));
      EXPECT_EQ(state.create_count, create_count + 1);
      EXPECT_EQ(state.close_count, close_count + 1);

      EXPECT_TRUE(CloseHandle(hDevice));
    }
  }
}

TEST(ntl_device, opens_through_dos_device_symbolic_link) {
  HANDLE hDevice =
      CreateFileW(L"\\\\.\\CrtSysTestDevice", GENERIC_READ | GENERIC_WRITE, 0,
                  NULL, OPEN_EXISTING, 0, NULL);
  EXPECT_NE(hDevice, INVALID_HANDLE_VALUE);

  if (hDevice != INVALID_HANDLE_VALUE) {
    DWORD bytes_returned = 0;
    test_device_state state = {};
    EXPECT_TRUE(DeviceIoControl(hDevice, TEST_DEVICE_STATE_CTL, NULL, 0,
                                &state, sizeof(state), &bytes_returned, NULL));
    EXPECT_EQ(bytes_returned, sizeof(state));
    EXPECT_TRUE(CloseHandle(hDevice));
  }
}

int main(int argc, char **argv) {
  testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
