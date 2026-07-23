#include <Windows.h>
#include <fltUser.h>

#include <cstdlib>
#include <cstdio>
#include <cwchar>
#include <stdexcept>

namespace {

constexpr wchar_t default_filter_name[] = L"CrtSysFltRuntimeTest";
constexpr wchar_t default_port_name[] = L"\\CrtSysFltRuntimePort";

unsigned parse_count(int argc, wchar_t **argv) {
  if (argc < 2)
    return 16;
  const auto value = std::wcstoul(argv[1], nullptr, 10);
  if (value == 0 || value > 100000)
    throw std::invalid_argument("iteration count is outside the test range");
  return static_cast<unsigned>(value);
}

[[noreturn]] void throw_filter_error(const char *operation,
                                     HRESULT result) {
  char message[128]{};
  std::snprintf(message, sizeof(message), "%s failed: HRESULT=0x%08lX",
                operation, static_cast<unsigned long>(result));
  throw std::runtime_error(message);
}

void enable_load_driver_privilege() {
  HANDLE token = nullptr;
  if (!OpenProcessToken(GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY,
                        &token))
    throw_filter_error("OpenProcessToken", HRESULT_FROM_WIN32(GetLastError()));

  LUID privilege{};
  if (!LookupPrivilegeValueW(nullptr, L"SeLoadDriverPrivilege", &privilege)) {
    const auto error = GetLastError();
    CloseHandle(token);
    throw_filter_error("LookupPrivilegeValueW", HRESULT_FROM_WIN32(error));
  }

  TOKEN_PRIVILEGES privileges{};
  privileges.PrivilegeCount = 1;
  privileges.Privileges[0].Luid = privilege;
  privileges.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;
  SetLastError(ERROR_SUCCESS);
  const BOOL adjusted = AdjustTokenPrivileges(token, FALSE, &privileges,
                                               sizeof(privileges), nullptr,
                                               nullptr);
  const auto error = GetLastError();
  CloseHandle(token);
  if (!adjusted || error == ERROR_NOT_ALL_ASSIGNED)
    throw_filter_error("AdjustTokenPrivileges", HRESULT_FROM_WIN32(error));
}

void connect_and_close(const wchar_t *port_name) {
  HANDLE raw = INVALID_HANDLE_VALUE;
  const HRESULT result = FilterConnectCommunicationPort(
      port_name, 0, nullptr, 0, nullptr, &raw);
  if (FAILED(result))
    throw_filter_error("FilterConnectCommunicationPort", result);
  if (!CloseHandle(raw))
    throw std::runtime_error("CloseHandle failed");
}

void reload_filter(const wchar_t *filter_name) {
  const HRESULT unloaded = FilterUnload(filter_name);
  if (FAILED(unloaded))
    throw_filter_error("FilterUnload", unloaded);

  const HRESULT loaded = FilterLoad(filter_name);
  if (FAILED(loaded))
    throw_filter_error("FilterLoad", loaded);
}

} // namespace

int wmain(int argc, wchar_t **argv) {
  try {
    const unsigned iterations = parse_count(argc, argv);
    const wchar_t *const filter_name = argc > 2 ? argv[2] : default_filter_name;
    const wchar_t *const port_name = argc > 3 ? argv[3] : default_port_name;

    enable_load_driver_privilege();

    for (unsigned index = 0; index != iterations; ++index) {
      connect_and_close(port_name);
      reload_filter(filter_name);
    }

    std::wprintf(L"Minifilter verifier lifecycle stress PASS: iterations=%u\n",
                 iterations);
    return 0;
  } catch (const std::exception &error) {
    std::fprintf(stderr, "Minifilter verifier lifecycle stress failure: %s\n",
                 error.what());
    return 1;
  }
}
