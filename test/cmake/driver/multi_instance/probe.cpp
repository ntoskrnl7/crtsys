#include <windows.h>

#include <cstdint>
#include <cstdio>

#include "common/multi_instance.h"

namespace {

int fail(const char *message) {
  std::printf("[CrtSysMultiProbe] FAIL %s GetLastError=%lu\n", message,
              GetLastError());
  return 1;
}

int send_request(HANDLE device, unsigned long command, std::uint64_t tls_value,
                 unsigned long last_error_value,
                 crtsys_multi_instance_response &response) {
  crtsys_multi_instance_request request{};
  request.command = command;
  request.tls_value = tls_value;
  request.last_error_value = last_error_value;

  DWORD returned = 0;
  if (!DeviceIoControl(device, CRTSYS_MULTI_INSTANCE_IOCTL, &request,
                       sizeof(request), &response, sizeof(response), &returned,
                       nullptr)) {
    return fail("DeviceIoControl");
  }

  if (returned != sizeof(response)) {
    std::printf("[CrtSysMultiProbe] FAIL unexpected byte count %lu\n",
                returned);
    return 1;
  }

  return 0;
}

int expect_response(const char *label,
                    const crtsys_multi_instance_response &response,
                    unsigned long instance_id, std::uint64_t tls_value,
                    unsigned long last_error_value,
                    unsigned long local_static_value) {
  if (response.instance_id != instance_id || response.tls_value != tls_value ||
      response.last_error_value != last_error_value ||
      response.local_static_value != local_static_value ||
      response.thread == 0 ||
      response.compiler_tls_index == 0xFFFFFFFFul ||
      response.crtsys_compiler_tls_index == 0xFFFFFFFFul) {
    std::printf(
        "[CrtSysMultiProbe] FAIL %s Instance=%lu Tls=0x%llx LastError=%lu "
        "Static=%lu CompilerTlsIndex=%lu CrtSysCompilerTlsIndex=%lu "
        "Thread=0x%llx\n",
        label, response.instance_id,
        static_cast<unsigned long long>(response.tls_value),
        response.last_error_value, response.local_static_value,
        response.compiler_tls_index, response.crtsys_compiler_tls_index,
        static_cast<unsigned long long>(response.thread));
    return 1;
  }

  return 0;
}

} // namespace

int main() {
  HANDLE device_a =
      CreateFileW(L"\\\\?\\Global\\GLOBALROOT\\Device\\" CRTSYS_MULTI_DEVICE_A,
                  GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, 0,
                  nullptr);
  if (device_a == INVALID_HANDLE_VALUE) {
    return fail("CreateFileW(crtsys_multi_a)");
  }

  HANDLE device_b =
      CreateFileW(L"\\\\?\\Global\\GLOBALROOT\\Device\\" CRTSYS_MULTI_DEVICE_B,
                  GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, 0,
                  nullptr);
  if (device_b == INVALID_HANDLE_VALUE) {
    CloseHandle(device_a);
    return fail("CreateFileW(crtsys_multi_b)");
  }

  crtsys_multi_instance_response a1{};
  crtsys_multi_instance_response b1{};
  crtsys_multi_instance_response a2{};
  crtsys_multi_instance_response b2{};
  constexpr std::uint64_t a_value = 0xA11DA11DA11DA11Dull;
  constexpr std::uint64_t b_value = 0xB22DB22DB22DB22Dull;
  constexpr unsigned long a_error = 0xA11D;
  constexpr unsigned long b_error = 0xB22D;

  const int result =
      send_request(device_a, CRTSYS_MULTI_INSTANCE_SET_QUERY, a_value, a_error,
                   a1) ||
      send_request(device_b, CRTSYS_MULTI_INSTANCE_SET_QUERY, b_value, b_error,
                   b1) ||
      send_request(device_a, CRTSYS_MULTI_INSTANCE_QUERY, 0, 0, a2) ||
      send_request(device_b, CRTSYS_MULTI_INSTANCE_QUERY, 0, 0, b2) ||
      expect_response("A initial", a1, 1, a_value, a_error, 1007) ||
      expect_response("B initial", b1, 2, b_value, b_error, 2007) ||
      expect_response("A after B", a2, 1, a_value, a_error, 1007) ||
      expect_response("B after A", b2, 2, b_value, b_error, 2007);

  if (result == 0) {
    if (a1.thread != b1.thread || a1.thread != a2.thread ||
        b1.thread != b2.thread) {
      std::printf(
          "[CrtSysMultiProbe] FAIL calls did not stay on one thread "
          "A1=0x%llx B1=0x%llx A2=0x%llx B2=0x%llx\n",
          static_cast<unsigned long long>(a1.thread),
          static_cast<unsigned long long>(b1.thread),
          static_cast<unsigned long long>(a2.thread),
          static_cast<unsigned long long>(b2.thread));
      CloseHandle(device_b);
      CloseHandle(device_a);
      return 1;
    }

    if (a1.compiler_tls_index != a2.compiler_tls_index ||
        b1.compiler_tls_index != b2.compiler_tls_index ||
        a1.compiler_tls_index == b1.compiler_tls_index) {
      std::printf(
          "[CrtSysMultiProbe] FAIL compiler TLS indexes are not stable and "
          "distinct A1=%lu B1=%lu A2=%lu B2=%lu "
          "CrtSysA1=%lu CrtSysB1=%lu CrtSysA2=%lu CrtSysB2=%lu "
          "BitmapA=0x%lx BitmapB=0x%lx RuntimeA=0x%llx RuntimeB=0x%llx "
          "CanonicalA=0x%llx CanonicalB=0x%llx InstalledA=0x%llx "
          "InstalledB=0x%llx\n",
          a1.compiler_tls_index, b1.compiler_tls_index,
          a2.compiler_tls_index, b2.compiler_tls_index,
          a1.crtsys_compiler_tls_index, b1.crtsys_compiler_tls_index,
          a2.crtsys_compiler_tls_index, b2.crtsys_compiler_tls_index,
          a1.crtsys_compiler_tls_bitmap0, b1.crtsys_compiler_tls_bitmap0,
          static_cast<unsigned long long>(a1.crtsys_compiler_tls_runtime),
          static_cast<unsigned long long>(b1.crtsys_compiler_tls_runtime),
          static_cast<unsigned long long>(
              a1.crtsys_compiler_tls_canonical_slots),
          static_cast<unsigned long long>(
              b1.crtsys_compiler_tls_canonical_slots),
          static_cast<unsigned long long>(
              a1.crtsys_compiler_tls_installed_slots),
          static_cast<unsigned long long>(
              b1.crtsys_compiler_tls_installed_slots));
      CloseHandle(device_b);
      CloseHandle(device_a);
      return 1;
    }

    std::printf(
        "[CrtSysMultiProbe] PASS A_TlsIndex=%lu B_TlsIndex=%lu "
        "A_Tls=0x%llx B_Tls=0x%llx A_LastError=%lu B_LastError=%lu "
        "A_Static=%lu B_Static=%lu A_CompilerTlsIndex=%lu "
        "B_CompilerTlsIndex=%lu A_CrtSysCompilerTlsIndex=%lu "
        "B_CrtSysCompilerTlsIndex=%lu BitmapA=0x%lx BitmapB=0x%lx "
        "RuntimeA=0x%llx RuntimeB=0x%llx Thread=0x%llx\n",
        a1.tls_index, b1.tls_index,
        static_cast<unsigned long long>(a1.tls_value),
        static_cast<unsigned long long>(b1.tls_value), a1.last_error_value,
        b1.last_error_value, a1.local_static_value, b1.local_static_value,
        a1.compiler_tls_index, b1.compiler_tls_index,
        a1.crtsys_compiler_tls_index, b1.crtsys_compiler_tls_index,
        a1.crtsys_compiler_tls_bitmap0, b1.crtsys_compiler_tls_bitmap0,
        static_cast<unsigned long long>(a1.crtsys_compiler_tls_runtime),
        static_cast<unsigned long long>(b1.crtsys_compiler_tls_runtime),
        static_cast<unsigned long long>(a1.thread));
  }

  CloseHandle(device_b);
  CloseHandle(device_a);
  return result == 0 ? 0 : 1;
}
