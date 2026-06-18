#pragma once

#define TEST_DEVICE_NAME L"test_device"

#define TEST_DEVICE_CTL                                                        \
  CTL_CODE(FILE_DEVICE_UNKNOWN, 0, METHOD_BUFFERED, FILE_ANY_ACCESS)

#define TEST_DEVICE_STATE_CTL                                                  \
  CTL_CODE(FILE_DEVICE_UNKNOWN, 1, METHOD_BUFFERED, FILE_ANY_ACCESS)

struct test_device_state {
  int create_count;
  int close_count;
};
