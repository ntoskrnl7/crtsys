#pragma once

#define CRTSYS_MULTI_DEVICE_A L"crtsys_multi_a"
#define CRTSYS_MULTI_DEVICE_B L"crtsys_multi_b"

#define CRTSYS_MULTI_INSTANCE_IOCTL                                            \
  CTL_CODE(FILE_DEVICE_UNKNOWN, 0x801, METHOD_BUFFERED, FILE_ANY_ACCESS)

#define CRTSYS_MULTI_INSTANCE_SET_QUERY 1u
#define CRTSYS_MULTI_INSTANCE_QUERY 2u

struct crtsys_multi_instance_request {
  unsigned long command;
  unsigned __int64 tls_value;
  unsigned long last_error_value;
};

struct crtsys_multi_instance_response {
  unsigned long instance_id;
  unsigned long tls_index;
  unsigned __int64 thread;
  unsigned __int64 tls_value;
  unsigned long last_error_value;
  unsigned long local_static_value;
  unsigned long compiler_tls_index;
  unsigned long crtsys_compiler_tls_index;
  unsigned long crtsys_compiler_tls_bitmap0;
  unsigned __int64 crtsys_compiler_tls_runtime;
  unsigned __int64 crtsys_compiler_tls_canonical_slots;
  unsigned __int64 crtsys_compiler_tls_installed_slots;
};
