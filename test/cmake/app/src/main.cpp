#include <gtest/gtest.h>
#include <windows.h>

#include <chrono>
#include <cstddef>
#include <cstring>
#include <string>
#include <system_error>
#include <type_traits>

#include <ntl/rpc/client>
#include <ntl/ipc/all>
#include <ntl/flt/communication_client>
// rpc client stub code
#include "common/rpc.hpp"

extern "C" std::size_t ntl_rpc_cxx14_async_operation_size() noexcept {
  return sizeof(ntl::rpc::detail::async_operation);
}

namespace {
static_assert(sizeof(ntl::ipc::mapped_buffer_descriptor) == 40,
              "mapped descriptors must remain x86/x64 stable");
static_assert(std::is_move_constructible<ntl::flt::communication_client>::value,
              "The minifilter app client must remain movable");

struct ipc_app_record {
  std::uint32_t sequence;
  std::uint32_t value;
};
} // namespace

TEST(ntl_ipc, pointer_free_region_and_bounded_ring) {
  constexpr ntl::ipc::region_handle handle{3, 5};
  alignas(64) unsigned char region_storage[512]{};
  ntl::ipc::region_view region{
      handle, region_storage, sizeof(region_storage),
      ntl::ipc::region_access::driver_read_write};

  ntl::ipc::buffer_token token;
  ASSERT_EQ(region.token(32, 128, token),
            ntl::ipc::validation_status::success);
  EXPECT_EQ(token.region, handle);

  ntl::ipc::mutable_buffer_view resolved;
  EXPECT_EQ(region.resolve(token, ntl::ipc::region_access::driver_write,
                           resolved),
            ntl::ipc::validation_status::success);
  EXPECT_EQ(resolved.data(), region_storage + 32);
  EXPECT_EQ(resolved.size(), 128u);

  using ring_type = ntl::ipc::shared_ring<ipc_app_record, 4>;
  alignas(64) unsigned char storage[ring_type::required_bytes()]{};
  ring_type producer;
  ASSERT_EQ(ring_type::initialize(storage, sizeof(storage), producer, 9),
            ntl::ipc::validation_status::success);
  ring_type consumer;
  ASSERT_EQ(ring_type::attach(storage, sizeof(storage), consumer),
            ntl::ipc::validation_status::success);

  for (std::uint32_t index = 0; index != 4; ++index)
    EXPECT_TRUE(producer.try_write({index, index + 100}));
  EXPECT_FALSE(producer.try_write({4, 104}));

  ipc_app_record value{};
  for (std::uint32_t index = 0; index != 4; ++index) {
    ASSERT_TRUE(consumer.try_read(value));
    EXPECT_EQ(value.sequence, index);
    EXPECT_EQ(value.value, index + 100);
  }
  EXPECT_FALSE(consumer.try_read(value));
}

TEST(ntl_ipc, mapped_descriptor_validation) {
  alignas(16) unsigned char storage[64]{};
  ntl::ipc::mapped_buffer_descriptor descriptor{
      7, 11,
      static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(storage)),
      sizeof(storage),
      static_cast<std::uint32_t>(ntl::ipc::map_access::read_write), 0};

  ntl::ipc::mapped_client_buffer mapped;
  EXPECT_EQ(ntl::ipc::mapped_client_buffer::open(descriptor, 11, mapped),
            ntl::ipc::validation_status::success);
  ASSERT_TRUE(mapped);
  EXPECT_EQ(mapped.data(), storage);
  EXPECT_EQ(mapped.writable_data(), storage);
  EXPECT_EQ(mapped.size(), sizeof(storage));

  EXPECT_EQ(ntl::ipc::mapped_client_buffer::open(descriptor, 12, mapped),
            ntl::ipc::validation_status::stale_region);
  EXPECT_FALSE(mapped);

  descriptor.access = 0xFFFFFFFFu;
  EXPECT_EQ(ntl::ipc::mapped_client_buffer::open(descriptor, 11, mapped),
            ntl::ipc::validation_status::access_denied);

  descriptor.access =
      static_cast<std::uint32_t>(ntl::ipc::map_access::read);
  EXPECT_EQ(ntl::ipc::mapped_client_buffer::open(descriptor, 11, mapped),
            ntl::ipc::validation_status::success);
  EXPECT_EQ(mapped.writable_data(), nullptr);

  descriptor.mapped_address =
      (std::numeric_limits<std::uint64_t>::max)() - 3;
  descriptor.length = 8;
  EXPECT_NE(ntl::ipc::mapped_client_buffer::open(descriptor, 11, mapped),
            ntl::ipc::validation_status::success);
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

namespace {

HANDLE open_test_device() noexcept {
  return CreateFileW(L"\\\\.\\CrtSysTestDevice",
                     GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING,
                     0, nullptr);
}

bool address_is_unmapped(std::uint64_t address) noexcept {
  unsigned char value = 0;
  SIZE_T copied = 0;
  const auto pointer =
      reinterpret_cast<const void *>(static_cast<std::uintptr_t>(address));
  return ReadProcessMemory(GetCurrentProcess(), pointer, &value, sizeof(value),
                           &copied) == FALSE &&
         copied == 0;
}

bool begin_user_mapping(HANDLE device, test_mapping_begin_result &result) {
  DWORD returned = 0;
  return DeviceIoControl(device, TEST_DEVICE_MAPPING_BEGIN_CTL, nullptr, 0,
                         &result, sizeof(result), &returned, nullptr) != FALSE &&
         returned == sizeof(result);
}

int run_mapping_exit_child() {
  const HANDLE device = open_test_device();
  if (device == INVALID_HANDLE_VALUE)
    return 70;

  test_mapping_begin_result result{};
  if (!begin_user_mapping(device, result))
    return 71;
  ntl::ipc::mapped_buffer_descriptor descriptor{
      result.mapping_id, result.generation, result.mapped_address,
      result.length, result.access, result.reserved};
  ntl::ipc::mapped_client_buffer mapped;
  if (ntl::ipc::mapped_client_buffer::open(descriptor, result.generation,
                                           mapped) !=
      ntl::ipc::validation_status::success)
    return 72;
  std::memset(mapped.writable_data(), test_mapping_user_value,
              result.pattern_bytes);
  TerminateProcess(GetCurrentProcess(), 73);
  return 74;
}

} // namespace

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

TEST(ntl_device, mapped_buffer_forced_unmap_and_disconnect) {
  HANDLE device = open_test_device();
  ASSERT_NE(device, INVALID_HANDLE_VALUE);

  test_mapping_begin_result begin{};
  ASSERT_TRUE(begin_user_mapping(device, begin));
  SYSTEM_INFO system_info{};
  GetSystemInfo(&system_info);
  ASSERT_EQ(begin.length,
            static_cast<unsigned long long>(system_info.dwPageSize));
  ASSERT_EQ(begin.pattern_bytes, test_mapping_pattern_bytes);

  const ntl::ipc::mapped_buffer_descriptor descriptor{
      begin.mapping_id, begin.generation, begin.mapped_address, begin.length,
      begin.access, begin.reserved};
  ntl::ipc::mapped_client_buffer mapped;
  ASSERT_EQ(ntl::ipc::mapped_client_buffer::open(
                descriptor, begin.generation, mapped),
            ntl::ipc::validation_status::success);
  ASSERT_NE(mapped.writable_data(), nullptr);
  auto *const bytes = static_cast<unsigned char *>(mapped.writable_data());
  for (unsigned long index = 0; index != begin.pattern_bytes; ++index)
    ASSERT_EQ(bytes[index],
              static_cast<unsigned char>(test_mapping_initial_seed + index));
  std::memset(bytes, test_mapping_user_value, begin.pattern_bytes);

  const test_mapping_close_request close_request{
      test_mapping_user_value, {0, 0, 0, 0, 0, 0, 0}};
  test_mapping_close_result close_result{};
  DWORD returned = 0;
  ASSERT_TRUE(DeviceIoControl(device, TEST_DEVICE_MAPPING_CLOSE_CTL,
                              const_cast<test_mapping_close_request *>(
                                  &close_request),
                              sizeof(close_request), &close_result,
                              sizeof(close_result), &returned, nullptr));
  EXPECT_EQ(returned, sizeof(close_result));
  EXPECT_EQ(close_result.observed_bytes, test_mapping_pattern_bytes);
  EXPECT_EQ(close_result.mappings_before, 1u);
  EXPECT_EQ(close_result.mappings_after, 0u);
  EXPECT_NE(close_result.generation_before, close_result.generation_after);
  EXPECT_TRUE(address_is_unmapped(begin.mapped_address));
  ASSERT_TRUE(CloseHandle(device));

  device = open_test_device();
  ASSERT_NE(device, INVALID_HANDLE_VALUE);
  begin = {};
  ASSERT_TRUE(begin_user_mapping(device, begin));
  ASSERT_TRUE(CloseHandle(device));
  EXPECT_TRUE(address_is_unmapped(begin.mapped_address));

  device = open_test_device();
  ASSERT_NE(device, INVALID_HANDLE_VALUE);
  test_device_state state{};
  returned = 0;
  ASSERT_TRUE(DeviceIoControl(device, TEST_DEVICE_STATE_CTL, nullptr, 0, &state,
                              sizeof(state), &returned, nullptr));
  EXPECT_EQ(returned, sizeof(state));
  EXPECT_EQ(state.active_mappings, 0u);
  EXPECT_GE(state.explicit_mapping_closes, 1u);
  EXPECT_GE(state.cleanup_mapping_closes, 1u);
  EXPECT_TRUE(CloseHandle(device));
}

TEST(ntl_device, automatic_irp_io_buffer_semantics_and_copy_back) {
  const HANDLE device = open_test_device();
  ASSERT_NE(device, INVALID_HANDLE_VALUE);
  DWORD returned = 0;

  test_auto_io_buffer buffered{test_auto_io_magic, 0x12345678};
  ASSERT_TRUE(DeviceIoControl(device, TEST_DEVICE_AUTO_BUFFERED_CTL, &buffered,
                              sizeof(buffered), &buffered, sizeof(buffered),
                              &returned, nullptr));
  EXPECT_EQ(returned, sizeof(buffered));
  EXPECT_EQ(buffered.magic, test_auto_io_magic);
  EXPECT_EQ(buffered.value, 0x12345678u ^ test_auto_io_mask);

  const test_auto_io_header buffered_tail_header{test_auto_io_magic,
                                                 0x10293847};
  test_auto_io_extended_result buffered_tail_result{};
  ASSERT_TRUE(DeviceIoControl(
      device, TEST_DEVICE_AUTO_BUFFERED_TAIL_CTL,
      const_cast<test_auto_io_header *>(&buffered_tail_header),
      sizeof(buffered_tail_header), &buffered_tail_result,
      sizeof(buffered_tail_result), &returned, nullptr));
  EXPECT_EQ(returned, sizeof(buffered_tail_result));
  EXPECT_EQ(buffered_tail_result.magic, test_auto_io_magic);
  EXPECT_EQ(buffered_tail_result.value,
            buffered_tail_header.sequence ^ test_auto_io_mask);
  EXPECT_EQ(buffered_tail_result.tail_zero_0, 0u);
  EXPECT_EQ(buffered_tail_result.tail_zero_1, 0u);

  const test_auto_io_header in_direct_header{test_auto_io_magic, 0x13572468};
  test_auto_io_buffer in_direct_payload{test_auto_io_magic,
                                        in_direct_header.sequence};
  returned = 99;
  ASSERT_TRUE(DeviceIoControl(
      device, TEST_DEVICE_AUTO_IN_DIRECT_CTL,
      const_cast<test_auto_io_header *>(&in_direct_header),
      sizeof(in_direct_header), &in_direct_payload, sizeof(in_direct_payload),
      &returned, nullptr));
  EXPECT_EQ(returned, 0u);

  const test_auto_io_header out_direct_header{test_auto_io_magic, 0x24681357};
  test_auto_io_result out_direct_result{};
  ASSERT_TRUE(DeviceIoControl(
      device, TEST_DEVICE_AUTO_OUT_DIRECT_CTL,
      const_cast<test_auto_io_header *>(&out_direct_header),
      sizeof(out_direct_header), &out_direct_result, sizeof(out_direct_result),
      &returned, nullptr));
  EXPECT_EQ(returned, sizeof(out_direct_result));
  EXPECT_EQ(out_direct_result.magic, test_auto_io_magic);
  EXPECT_EQ(out_direct_result.value,
            out_direct_header.sequence ^ test_auto_io_mask);

  const test_auto_io_header neither_header{test_auto_io_magic, 0x11223344};
  test_auto_io_result neither_result{};
  ASSERT_TRUE(DeviceIoControl(
      device, TEST_DEVICE_AUTO_NEITHER_CTL,
      const_cast<test_auto_io_header *>(&neither_header),
      sizeof(neither_header), &neither_result, sizeof(neither_result),
      &returned, nullptr));
  EXPECT_EQ(returned, sizeof(neither_result));
  EXPECT_EQ(neither_result.magic, test_auto_io_magic);
  EXPECT_EQ(neither_result.value, neither_header.sequence ^ test_auto_io_mask);

  EXPECT_TRUE(CloseHandle(device));
}

TEST(ntl_device, process_exit_closes_mapped_view) {
  HANDLE probe = open_test_device();
  ASSERT_NE(probe, INVALID_HANDLE_VALUE);
  test_device_state before{};
  DWORD returned = 0;
  ASSERT_TRUE(DeviceIoControl(probe, TEST_DEVICE_STATE_CTL, nullptr, 0, &before,
                              sizeof(before), &returned, nullptr));
  ASSERT_TRUE(CloseHandle(probe));

  wchar_t executable[MAX_PATH]{};
  ASSERT_NE(GetModuleFileNameW(nullptr, executable, MAX_PATH), 0u);
  std::wstring command = L"\"";
  command += executable;
  command += L"\" --ntl-mapping-exit-child";
  STARTUPINFOW startup{};
  startup.cb = sizeof(startup);
  PROCESS_INFORMATION process{};
  ASSERT_TRUE(CreateProcessW(nullptr, &command[0], nullptr, nullptr, FALSE, 0,
                             nullptr, nullptr, &startup, &process));
  ASSERT_EQ(WaitForSingleObject(process.hProcess, 10000), WAIT_OBJECT_0);
  DWORD exit_code = 0;
  ASSERT_TRUE(GetExitCodeProcess(process.hProcess, &exit_code));
  EXPECT_EQ(exit_code, 73u);
  CloseHandle(process.hThread);
  CloseHandle(process.hProcess);

  HANDLE device = INVALID_HANDLE_VALUE;
  for (unsigned attempt = 0; attempt != 100; ++attempt) {
    device = open_test_device();
    if (device != INVALID_HANDLE_VALUE)
      break;
    Sleep(20);
  }
  ASSERT_NE(device, INVALID_HANDLE_VALUE);
  test_device_state after{};
  returned = 0;
  ASSERT_TRUE(DeviceIoControl(device, TEST_DEVICE_STATE_CTL, nullptr, 0, &after,
                              sizeof(after), &returned, nullptr));
  EXPECT_EQ(after.active_mappings, 0u);
  EXPECT_GT(after.cleanup_mapping_closes, before.cleanup_mapping_closes);
  EXPECT_TRUE(CloseHandle(device));
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
  if (argc == 2 && std::strcmp(argv[1], "--ntl-mapping-exit-child") == 0)
    return run_mapping_exit_child();
  testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
