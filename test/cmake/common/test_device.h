#pragma once

#define TEST_DEVICE_NAME L"test_device"

#define TEST_DEVICE_CTL                                                        \
  CTL_CODE(FILE_DEVICE_UNKNOWN, 0, METHOD_BUFFERED, FILE_ANY_ACCESS)

#define TEST_DEVICE_STATE_CTL                                                  \
  CTL_CODE(FILE_DEVICE_UNKNOWN, 1, METHOD_BUFFERED, FILE_ANY_ACCESS)

#define TEST_DEVICE_MAPPING_BEGIN_CTL                                          \
  CTL_CODE(FILE_DEVICE_UNKNOWN, 2, METHOD_BUFFERED, FILE_ANY_ACCESS)

#define TEST_DEVICE_MAPPING_CLOSE_CTL                                          \
  CTL_CODE(FILE_DEVICE_UNKNOWN, 3, METHOD_BUFFERED, FILE_ANY_ACCESS)

#define TEST_DEVICE_AUTO_BUFFERED_CTL                                          \
  CTL_CODE(FILE_DEVICE_UNKNOWN, 4, METHOD_BUFFERED, FILE_ANY_ACCESS)

#define TEST_DEVICE_AUTO_IN_DIRECT_CTL                                         \
  CTL_CODE(FILE_DEVICE_UNKNOWN, 5, METHOD_IN_DIRECT, FILE_ANY_ACCESS)

#define TEST_DEVICE_AUTO_OUT_DIRECT_CTL                                        \
  CTL_CODE(FILE_DEVICE_UNKNOWN, 6, METHOD_OUT_DIRECT, FILE_ANY_ACCESS)

#define TEST_DEVICE_AUTO_NEITHER_CTL                                           \
  CTL_CODE(FILE_DEVICE_UNKNOWN, 7, METHOD_NEITHER, FILE_ANY_ACCESS)

#define TEST_DEVICE_AUTO_BUFFERED_TAIL_CTL                                     \
  CTL_CODE(FILE_DEVICE_UNKNOWN, 8, METHOD_BUFFERED, FILE_ANY_ACCESS)

constexpr unsigned long test_mapping_pattern_bytes = 256;
constexpr unsigned char test_mapping_initial_seed = 0x31;
constexpr unsigned char test_mapping_user_value = 0xA5;
constexpr unsigned long test_auto_io_magic = 0x4D50494F;
constexpr unsigned long test_auto_io_mask = 0x5AA55AA5;

struct test_device_state {
  int create_count;
  int close_count;
  unsigned long active_mappings;
  unsigned long explicit_mapping_closes;
  unsigned long cleanup_mapping_closes;
};

struct test_mapping_begin_result {
  unsigned long long mapping_id;
  unsigned long long generation;
  unsigned long long mapped_address;
  unsigned long long length;
  unsigned long access;
  unsigned long reserved;
  unsigned long pattern_bytes;
  unsigned long reserved2;
};

struct test_mapping_close_request {
  unsigned char expected_value;
  unsigned char reserved[7];
};

struct test_mapping_close_result {
  unsigned long long generation_before;
  unsigned long long generation_after;
  unsigned long observed_bytes;
  unsigned long mappings_before;
  unsigned long mappings_after;
  unsigned long reserved;
};

struct test_auto_io_buffer {
  unsigned long magic;
  unsigned long value;
};

struct test_auto_io_header {
  unsigned long magic;
  unsigned long sequence;
};

struct test_auto_io_result {
  unsigned long magic;
  unsigned long value;
};

struct test_auto_io_extended_result {
  unsigned long magic;
  unsigned long value;
  unsigned long tail_zero_0;
  unsigned long tail_zero_1;
};

static_assert(sizeof(test_mapping_begin_result) == 48,
              "mapping test wire result must be x86/x64 stable");
static_assert(sizeof(test_mapping_close_request) == 8,
              "mapping close request must be x86/x64 stable");
static_assert(sizeof(test_mapping_close_result) == 32,
              "mapping close result must be x86/x64 stable");
static_assert(sizeof(test_auto_io_extended_result) == 16,
              "buffered tail test result must be x86/x64 stable");
