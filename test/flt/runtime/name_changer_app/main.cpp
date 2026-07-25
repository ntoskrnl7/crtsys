#include "../name_changer_shared/name_changer_runtime.hpp"
#include "directory_tests.hpp"
#include "namespace_tests.hpp"
#include "record_validation_tests.hpp"

#include <Windows.h>
#include <fltUser.h>

#include <ntl/flt/communication_client>

#include <algorithm>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <string_view>

namespace {

namespace fs = std::filesystem;
using namespace crtsys_flt_name_changer_runtime_test;

class loaded_filter {
public:
  loaded_filter() noexcept {
    status_ = FilterLoad(filter_name);
    loaded_ = SUCCEEDED(status_);
  }

  loaded_filter(const loaded_filter &) = delete;
  loaded_filter &operator=(const loaded_filter &) = delete;

  ~loaded_filter() { (void)unload(); }

  HRESULT status() const noexcept { return status_; }

  HRESULT unload() noexcept {
    if (!loaded_)
      return S_OK;
    const HRESULT result = FilterUnload(filter_name);
    if (SUCCEEDED(result))
      loaded_ = false;
    return result;
  }

private:
  HRESULT status_ = E_UNEXPECTED;
  bool loaded_ = false;
};

fs::path relative_mapping_path(std::wstring_view value) {
  while (!value.empty() && (value.front() == L'\\' || value.front() == L'/'))
    value.remove_prefix(1);
  return fs::path(value);
}

bool contains_name(const fs::path &directory, std::wstring_view name,
                   std::error_code &error) {
  error.clear();
  for (fs::directory_iterator current(directory, error), end;
       !error && current != end; current.increment(error)) {
    if (_wcsicmp(current->path().filename().c_str(),
                 std::wstring(name).c_str()) == 0)
      return true;
  }
  return false;
}

bool write_text(const fs::path &path, std::string_view value) {
  std::ofstream output(path, std::ios::binary | std::ios::trunc);
  if (!output)
    return false;
  output.write(value.data(), static_cast<std::streamsize>(value.size()));
  return output.good();
}

bool read_text(const fs::path &path, std::string &value) {
  std::ifstream input(path, std::ios::binary);
  if (!input)
    return false;
  value.assign(std::istreambuf_iterator<char>(input),
               std::istreambuf_iterator<char>());
  return input.good() || input.eof();
}

DWORD enable_load_driver_privilege() noexcept {
  HANDLE token = nullptr;
  if (!OpenProcessToken(GetCurrentProcess(),
                        TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, &token))
    return GetLastError();

  LUID identifier{};
  if (!LookupPrivilegeValueW(nullptr, L"SeLoadDriverPrivilege", &identifier)) {
    const DWORD error = GetLastError();
    CloseHandle(token);
    return error;
  }

  TOKEN_PRIVILEGES privileges{};
  privileges.PrivilegeCount = 1;
  privileges.Privileges[0].Luid = identifier;
  privileges.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;
  SetLastError(ERROR_SUCCESS);
  (void)AdjustTokenPrivileges(token, FALSE, &privileges, 0, nullptr, nullptr);
  const DWORD error = GetLastError();
  CloseHandle(token);
  return error;
}

} // namespace

int wmain(int argc, wchar_t **argv) {
  std::string record_validation_failure;
  if (!run_record_validation_tests(record_validation_failure)) {
    std::cerr << "output record validation self-test failed: "
              << record_validation_failure << '\n';
    return 1;
  }
  if (argc == 2 &&
      std::wstring_view(argv[1]) == L"--record-validation-only") {
    std::cout << "NTL NameChanger output record validation PASS\n";
    return 0;
  }

  bool run_find_by_sid_stress = false;
  fs::path requested_root;
  for (int index = 1; index < argc; ++index) {
    const std::wstring_view argument(argv[index]);
    if (argument == L"--find-by-sid-stress") {
      run_find_by_sid_stress = true;
    } else if (argument.starts_with(L"--") || !requested_root.empty()) {
      std::cerr << "usage: crtsys_flt_name_changer_runtime_test_app "
                   "[volume-root] [--find-by-sid-stress]\n";
      return 2;
    } else {
      requested_root = fs::path(argument);
    }
  }
  if (argc > 3) {
    std::cerr << "usage: crtsys_flt_name_changer_runtime_test_app "
                 "[volume-root] [--find-by-sid-stress]\n";
    return 2;
  }

  fs::path root = requested_root.empty() ? fs::current_path().root_path()
                                         : requested_root;
  root = root.root_path();
  if (root.empty()) {
    std::cerr << "a volume root such as C:\\ is required\n";
    return 2;
  }

  const DWORD privilege_error = enable_load_driver_privilege();
  if (privilege_error != ERROR_SUCCESS) {
    std::cerr << "failed to enable SeLoadDriverPrivilege: " << privilege_error
              << '\n';
    return 1;
  }

  const fs::path visible_mapping = root / relative_mapping_path(user_mapping);
  const fs::path visible_parent = visible_mapping.parent_path();
  const fs::path store_mapping = root / relative_mapping_path(real_mapping);
  const fs::path store_parent = store_mapping.parent_path();
  const fs::path visible_payload = visible_mapping / payload_name;
  const fs::path physical_payload = store_mapping / payload_name;
  const fs::path visible_created = visible_mapping / created_name;
  const fs::path physical_created = store_mapping / created_name;
  const fs::path visible_renamed = visible_mapping / renamed_name;
  const fs::path physical_renamed = store_mapping / renamed_name;
  const fs::path visible_hard_link = visible_mapping / hard_link_name;
  const fs::path physical_hard_link = store_mapping / hard_link_name;
  const fs::path physical_notification_renamed =
      store_mapping / notification_renamed_name;

  const HRESULT stale = FilterUnload(filter_name);
  if (FAILED(stale) &&
      stale != static_cast<HRESULT>(ERROR_FLT_FILTER_NOT_FOUND)) {
    std::wcerr << L"failed to unload a stale " << filter_name << L": 0x"
               << std::hex << std::uppercase
               << static_cast<unsigned long>(stale) << L'\n';
    return 1;
  }

  std::error_code error;
  fs::remove_all(visible_parent, error);
  error.clear();
  fs::remove_all(store_parent, error);
  error.clear();
  if (!fs::create_directories(visible_parent, error) || error) {
    std::cerr << "failed to create visible parent: " << error.message() << '\n';
    return 1;
  }
  if (!fs::create_directories(store_mapping, error) || error) {
    std::cerr << "failed to create backing mapping: " << error.message()
              << '\n';
    fs::remove_all(visible_parent, error);
    return 1;
  }
  if (!write_text(physical_payload, "NTL NameChanger payload\n")) {
    std::cerr << "failed to seed the backing file\n";
    fs::remove_all(visible_parent, error);
    fs::remove_all(store_parent, error);
    return 1;
  }
  if (run_find_by_sid_stress) {
    std::string stress_failure;
    if (!prepare_find_by_sid_stress(store_mapping, stress_failure)) {
      std::cerr << "failed to prepare Find-by-SID stress: " << stress_failure
                << '\n';
      fs::remove_all(visible_parent, error);
      fs::remove_all(store_parent, error);
      return 1;
    }
  }

  directory_enumeration_baseline directory_baseline;
  std::string directory_failure;
  if (!prepare_directory_enumeration_tests(visible_parent, store_parent,
                                           directory_baseline,
                                           directory_failure)) {
    std::cerr << "failed to prepare directory enumeration tests: "
              << directory_failure << '\n';
    fs::remove_all(visible_parent, error);
    fs::remove_all(store_parent, error);
    return 1;
  }
  const auto supported_directory_classes =
      std::count(directory_baseline.supported.begin(),
                 directory_baseline.supported.end(), true);
  std::cout << "directory_classes_supported=" << supported_directory_classes
            << '/' << directory_baseline.supported.size() << '\n';
  for (std::size_t index = 0; index != directory_baseline.supported.size();
       ++index) {
    if (!directory_baseline.supported[index])
      std::cout << "directory_class_unsupported="
                << directory_information_class_names[index] << '\n';
  }

  loaded_filter filter;
  if (FAILED(filter.status())) {
    std::wcerr << L"failed to load " << filter_name << L": 0x" << std::hex
               << std::uppercase << static_cast<unsigned long>(filter.status())
               << L'\n';
    fs::remove_all(visible_parent, error);
    fs::remove_all(store_parent, error);
    return 1;
  }

  auto fail = [&](std::string_view message) {
    std::cerr << message << '\n';
    (void)filter.unload();
    fs::remove_all(visible_parent, error);
    fs::remove_all(store_parent, error);
    return 1;
  };

  error.clear();
  if (!fs::exists(visible_payload, error) || error)
    return fail("the payload is not reachable through the visible mapping");

  std::string contents;
  if (!read_text(visible_payload, contents) ||
      contents != "NTL NameChanger payload\n")
    return fail("reading through the visible mapping returned wrong data");

  error.clear();
  if (fs::exists(physical_payload, error))
    return fail("the backing mapping remained directly reachable");

  try {
    auto client = ntl::flt::communication_client::connect(port_name);
    if (client.invoke(query_generated_name_count) == 0)
      return fail("the generated-name callback did not return a visible name");
  } catch (const std::exception &failure) {
    std::cerr << "name-provider observation failed: " << failure.what() << '\n';
    return fail("the app could not query generated-name observations");
  }

  directory_failure.clear();
  if (!run_directory_enumeration_tests(visible_parent, visible_mapping,
                                       store_parent, directory_baseline,
                                       directory_failure)) {
    return fail("directory enumeration verification failed: " +
                directory_failure);
  }
  const auto verified_file_id_64_classes =
      std::count(directory_baseline.supported.end() - 2,
                 directory_baseline.supported.end(), true);
  std::cout << "directory_classes_e2e_verified="
            << supported_directory_classes
            << " supported=" << supported_directory_classes
            << " total=" << directory_baseline.supported.size() << '\n';
  std::cout << "file_id_64_extd_directory_classes_e2e_verified="
            << verified_file_id_64_classes
            << " supported=" << verified_file_id_64_classes
            << " total=2\n";

  error.clear();
  if (!contains_name(visible_parent, user_mapping_final_component, error) ||
      error)
    return fail("directory enumeration did not inject the visible graft");

  error.clear();
  if (contains_name(store_parent, real_mapping_final_component, error) || error)
    return fail("directory enumeration exposed the backing directory");

  if (!write_text(visible_created, "created through visible graft\n"))
    return fail("writing through the visible mapping failed");

  std::string namespace_failure;
  namespace_feature_support namespace_support;
  if (!run_namespace_tests(visible_mapping, visible_created, visible_renamed,
                           visible_hard_link, run_find_by_sid_stress,
                           namespace_support,
                           namespace_failure))
    return fail("namespace verification failed: " + namespace_failure);

  try {
    auto client = ntl::flt::communication_client::connect(port_name);
    const observations observed = client.invoke(query_observations);
    std::cout << "namechanger_observations=" << observed.generated_names << '/'
              << observed.query_name_rewrites << '/' << observed.rename_reissues
              << '/' << observed.hard_link_reissues << '/'
              << observed.notification_requests << '/' << observed.usn_rewrites
              << '/' << observed.extended_directory_queries << '/'
              << observed.network_query_retries << '\n';
    std::cout << "synthetic_file_id_64_layouts="
              << observed.synthetic_file_id_64_layouts << "/2\n";
    std::cout << "hard_link_query_rewrites="
              << observed.hard_link_query_rewrites << '\n';
    std::cout << "fsctl_name_rewrites=" << observed.enum_usn_rewrites << '/'
              << observed.read_journal_rewrites << '/'
              << observed.lookup_cluster_rewrites << '/'
              << observed.find_by_sid_rewrites << '\n';
    if (observed.generated_names == 0 || observed.query_name_rewrites < 6 ||
        observed.rename_reissues < 2 || observed.hard_link_reissues == 0 ||
        observed.notification_requests < 2 ||
        observed.usn_rewrites < usn_query_iterations ||
        observed.extended_directory_queries < 4 ||
        observed.network_query_retries == 0 ||
        observed.synthetic_file_id_64_layouts != 2 ||
        observed.hard_link_query_rewrites == 0 ||
        (namespace_support.enum_usn && observed.enum_usn_rewrites == 0) ||
        (namespace_support.read_usn_journal &&
         observed.read_journal_rewrites == 0) ||
        (namespace_support.lookup_stream_from_cluster &&
         observed.lookup_cluster_rewrites == 0) ||
        (namespace_support.find_files_by_sid &&
         observed.find_by_sid_rewrites == 0)) {
      return fail("the driver did not observe every namespace operation");
    }
  } catch (const std::exception &failure) {
    std::cerr << "namespace observation failed: " << failure.what() << '\n';
    return fail("the app could not query namespace observations");
  }

  const HRESULT unloaded = filter.unload();
  if (FAILED(unloaded))
    return fail("the NameChanger runtime filter did not unload");

  contents.clear();
  if (!read_text(physical_renamed, contents) ||
      contents != "created through visible graft\n")
    return fail("the renamed file did not reach the backing directory");

  contents.clear();
  if (!read_text(physical_hard_link, contents) ||
      contents != "created through visible graft\n")
    return fail("the hard link did not reach the backing directory");

  contents.clear();
  if (!read_text(physical_notification_renamed, contents) ||
      contents != "notification\n")
    return fail("the notification rename did not reach the backing directory");

  error.clear();
  if (fs::exists(physical_created, error))
    return fail("the physical rename source still exists after rename");

  error.clear();
  if (fs::exists(visible_mapping, error))
    return fail("the test accidentally created a physical visible graft");

  fs::remove_all(visible_parent, error);
  error.clear();
  fs::remove_all(store_parent, error);

  std::cout << "NTL NameChanger runtime test PASS\n";
  return 0;
}
