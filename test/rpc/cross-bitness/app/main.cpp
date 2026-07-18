#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>

#include <cstdint>
#include <cstdio>
#include <exception>
#include <limits>
#include <string>

#include <ntl/rpc/client>

#include "rpc_cross_bitness.hpp"

namespace {

rpc_cross_bitness_payload make_boundary_payload() {
  rpc_cross_bitness_payload value{};
  value.unsigned8 = std::numeric_limits<std::uint8_t>::max();
  value.signed8 = std::numeric_limits<std::int8_t>::min();
  value.unsigned16 = std::numeric_limits<std::uint16_t>::max();
  value.signed16 = std::numeric_limits<std::int16_t>::min();
  value.unsigned32 = std::numeric_limits<std::uint32_t>::max();
  value.signed32 = std::numeric_limits<std::int32_t>::min();
  value.unsigned64 = std::numeric_limits<std::uint64_t>::max();
  value.signed64 = std::numeric_limits<std::int64_t>::min();
  value.float32 = 1.5f;
  value.float64 = -2.25;
  value.boolean = true;
  value.mode = rpc_wire_mode::boundary_values;
  value.text = std::string("A\0B", 3);
  value.wide_text = std::wstring(L"W\0Z", 3);
  value.bytes = {0, 1, 0x7F, 0x80, 0xFF};
  value.numbers = {0, 1, 0x7FFFFFFFu, 0x80000000u, 0xFFFFFFFFu};
  value.words = {"", "alpha", std::string("x\0y", 3)};
  value.matrix = {{}, {0, 1, 0xFFFFu}, {7, 8, 9, 10}};
  value.nested = {{-1, "negative", std::uint64_t{0}},
                  {42, "answer", std::uint64_t{0xFEDCBA9876543210ull}},
                  {7, "missing", std::nullopt}};
  value.fixed = {0, 0x0123456789ABCDEFull, 0xFFFFFFFFFFFFFFFFull};
  value.list_values = {std::numeric_limits<std::int32_t>::min(), -1, 0, 1,
                       std::numeric_limits<std::int32_t>::max()};
  value.deque_values = {"front", std::string("middle\0value", 12), "back"};
  value.set_values = {std::numeric_limits<std::int64_t>::min(), -1, 0, 1,
                      std::numeric_limits<std::int64_t>::max()};
  value.multiset_values = {-1, -1, 0, 1, 1};
  value.unordered_set_values = {0, 1, 0xFEDCBA9876543210ull};
  value.ordered_values = {{-1, "minus"}, {0, "zero"}, {1, "one"}};
  value.multimap_values = {{0, "zero-a"}, {0, "zero-b"}, {1, "one"}};
  value.unordered_values = {{0, L"zero"}, {1, L"one"}, {0xFFFFFFFFu, L"max"}};
  value.pair_value = {std::numeric_limits<std::int64_t>::min(),
                      std::numeric_limits<std::uint64_t>::max()};
  value.tuple_value = {std::uint8_t{0xFFu}, std::uint16_t{0xFFFFu},
                       std::uint32_t{0xFFFFFFFFu},
                       std::uint64_t{0xFFFFFFFFFFFFFFFFull}};
  value.optional_value = std::string("present\0value", 13);
  value.empty_optional = std::nullopt;
  value.variant_number = std::numeric_limits<std::int64_t>::min();
  value.variant_text = std::string("variant\0text", 12);
  value.portable_size = 0xFEDCBA9876543210ull;
  value.portable_difference = std::numeric_limits<std::int64_t>::min();
  return value;
}

bool expect_rejected_raw_request(std::uint64_t function,
                                 const void *input,
                                 DWORD input_size) {
  HANDLE device = CreateFileW(
      L"\\\\?\\Global\\GLOBALROOT\\Device\\crtsys_rpc_cross_bitness",
      GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, 0, nullptr);
  if (device == INVALID_HANDLE_VALUE)
    return false;

  DWORD returned = 0;
  const BOOL accepted = DeviceIoControl(
      device, ntl::rpc::control_code(static_cast<unsigned long>(function)),
      const_cast<void *>(input),
      input_size, nullptr, 0, &returned, nullptr);
  CloseHandle(device);
  return accepted == FALSE;
}

} // namespace

int wmain() {
  try {
    ntl::rpc::client client(L"crtsys_rpc_cross_bitness");

    const auto server =
        client.invoke(crtsys_rpc_cross_bitness::architecture);
    if (server.pointer_bits != 64 || server.size_bits != 64 ||
        server.difference_bits != 64 || server.wchar_bits != 16) {
      std::fwprintf(stderr,
                    L"architecture mismatch: client_ptr=%zu server_ptr=%u "
                    L"client_size=%zu server_size=%u\n",
                    sizeof(void *) * 8, server.pointer_bits,
                    sizeof(std::size_t) * 8, server.size_bits);
      return 1;
    }

    const auto boundary = make_boundary_payload();

    bool undersized_response_rejected = false;
    try {
      constexpr auto undersized_echo =
          ntl::rpc::method<0xA02,
                           rpc_cross_bitness_payload(
                               const rpc_cross_bitness_payload &),
                           1>{};
      (void)client.invoke(undersized_echo, boundary);
    } catch (const std::exception &) {
      undersized_response_rejected = true;
    }
    if (!undersized_response_rejected ||
        client.invoke(crtsys_rpc_cross_bitness::echo_count) != 0) {
      std::fwprintf(stderr,
                    L"undersized response executed the server callback\n");
      return 1;
    }

    const auto echoed =
        client.invoke(crtsys_rpc_cross_bitness::echo, boundary);
    if (!(echoed == boundary)) {
      std::fwprintf(stderr, L"boundary payload mismatch\n");
      return 1;
    }

    rpc_cross_bitness_payload empty{};
    const auto empty_echoed =
        client.invoke(crtsys_rpc_cross_bitness::echo, empty);
    if (!(empty_echoed == empty)) {
      std::fwprintf(stderr, L"empty payload mismatch\n");
      return 1;
    }

    auto large = boundary;
    large.bytes.resize(128 * 1024);
    large.numbers.resize(32 * 1024);
    for (std::size_t index = 0; index < large.bytes.size(); ++index)
      large.bytes[index] = static_cast<std::uint8_t>(index);
    for (std::size_t index = 0; index < large.numbers.size(); ++index)
      large.numbers[index] = static_cast<std::uint32_t>(index * 2654435761u);

    const auto large_echoed =
        client.invoke(crtsys_rpc_cross_bitness::echo, large);
    if (!(large_echoed == large)) {
      std::fwprintf(stderr, L"large payload mismatch\n");
      return 1;
    }

    bool truncated_reply_rejected = false;
    try {
      constexpr auto mismatched_narrow_reply =
          ntl::rpc::method<0xA03, std::uint64_t()>{};
      (void)client.invoke(mismatched_narrow_reply);
    } catch (const std::exception &) {
      truncated_reply_rejected = true;
    }
    if (!truncated_reply_rejected) {
      std::fwprintf(stderr, L"truncated reply was accepted\n");
      return 1;
    }

    const auto vector_size =
        client.invoke(crtsys_rpc_cross_bitness::vector_size,
                      std::vector<std::uint8_t>{1, 2, 3, 4});
    if (vector_size != 4) {
      std::fwprintf(stderr, L"valid vector request failed\n");
      return 1;
    }

    const std::uint8_t malformed = 0xFF;
    const std::uint32_t oversized_count = 0xFFFFFFFFu;
    if (!expect_rejected_raw_request(crtsys_rpc_cross_bitness::echo.id(),
                                     &malformed, sizeof(malformed)) ||
        !expect_rejected_raw_request(
            crtsys_rpc_cross_bitness::vector_size.id(), &oversized_count,
            sizeof(oversized_count)) ||
        !expect_rejected_raw_request(0xAFF, nullptr, 0)) {
      std::fwprintf(stderr, L"malformed RPC request was accepted\n");
      return 1;
    }

    const auto final_echo =
        client.invoke(crtsys_rpc_cross_bitness::echo, boundary);
    if (!(final_echo == boundary)) {
      std::fwprintf(stderr, L"RPC endpoint did not recover after rejection\n");
      return 1;
    }

    const wchar_t *const client_arch = sizeof(void *) == 4 ? L"x86" : L"x64";
    std::wprintf(L"RPC cross-bitness PASS: client=%ls server=x64 "
                 L"boundary=1 empty=1 large_bytes=%zu large_numbers=%zu "
                 L"bounded_response=1 malformed=3\n",
                 client_arch, large.bytes.size(), large.numbers.size());
    return 0;
  } catch (const std::exception &error) {
    std::fprintf(stderr, "RPC cross-bitness failure: %s\n", error.what());
    return 1;
  }
}
