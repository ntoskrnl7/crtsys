#include <Windows.h>
#include <fltUser.h>
#include <winternl.h>

#define NTL_USER_MODE
#include <ntl/flt/communication_client>

#include "../simrep_shared/simrep_runtime.hpp"

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>

namespace {

namespace fs = std::filesystem;
using namespace crtsys_flt_simrep_runtime_test;

struct file_network_open_information {
  LARGE_INTEGER creation_time;
  LARGE_INTEGER last_access_time;
  LARGE_INTEGER last_write_time;
  LARGE_INTEGER change_time;
  LARGE_INTEGER allocation_size;
  LARGE_INTEGER end_of_file;
  ULONG file_attributes;
};

using nt_query_full_attributes_file_fn =
    NTSTATUS(NTAPI *)(POBJECT_ATTRIBUTES, file_network_open_information *);

[[noreturn]] void fail(std::string_view operation,
                       DWORD error = GetLastError()) {
  std::ostringstream message;
  message << operation << " failed: error=" << error;
  throw std::runtime_error(message.str());
}

fs::path relative_mapping_path(std::wstring_view value) {
  while (!value.empty() && (value.front() == L'\\' || value.front() == L'/'))
    value.remove_prefix(1);
  return fs::path(value);
}

void write_text(const fs::path &path, std::string_view value) {
  std::ofstream output(path, std::ios::binary | std::ios::trunc);
  if (!output)
    fail("open output file");
  output.write(value.data(), static_cast<std::streamsize>(value.size()));
  if (!output.good())
    fail("write output file");
}

std::string read_text(const fs::path &path) {
  std::ifstream input(path, std::ios::binary);
  if (!input)
    fail("open input file");
  std::string value{std::istreambuf_iterator<char>(input),
                    std::istreambuf_iterator<char>()};
  if (!input.good() && !input.eof())
    fail("read input file");
  return value;
}

void arm_visible_passthrough_once(
    ntl::flt::communication_client &client) {
  if (client.invoke(arm_visible_passthrough) != 1)
    throw std::runtime_error("the driver rejected the passthrough arm request");
}

void create_physical_visible_root(ntl::flt::communication_client &client,
                                  const fs::path &visible) {
  arm_visible_passthrough_once(client);
  if (CreateDirectoryW(visible.c_str(), nullptr))
    return;
  const DWORD error = GetLastError();
  if (error != ERROR_ALREADY_EXISTS)
    fail("CreateDirectoryW(physical visible root)", error);
}

void remove_physical_visible_root(ntl::flt::communication_client &client,
                                  const fs::path &visible,
                                  bool required) {
  arm_visible_passthrough_once(client);
  if (RemoveDirectoryW(visible.c_str()))
    return;
  const DWORD error = GetLastError();
  if (error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND)
    return;
  if (required)
    fail("RemoveDirectoryW(physical visible root)", error);
}

void query_full_attributes(const fs::path &path) {
  const HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
  if (!ntdll)
    fail("GetModuleHandleW(ntdll)");
  const auto query = reinterpret_cast<nt_query_full_attributes_file_fn>(
      GetProcAddress(ntdll, "NtQueryFullAttributesFile"));
  if (!query)
    fail("GetProcAddress(NtQueryFullAttributesFile)");

  std::wstring native_name = LR"(\??\)";
  native_name.append(path.wstring());
  if (native_name.size() >
      static_cast<std::size_t>((std::numeric_limits<USHORT>::max)()) /
          sizeof(wchar_t)) {
    throw std::runtime_error("native query path is too long");
  }

  UNICODE_STRING name{};
  name.Buffer = native_name.data();
  name.Length =
      static_cast<USHORT>(native_name.size() * sizeof(wchar_t));
  name.MaximumLength = name.Length;
  OBJECT_ATTRIBUTES attributes{};
  InitializeObjectAttributes(&attributes, &name, OBJ_CASE_INSENSITIVE,
                             nullptr, nullptr);
  file_network_open_information information{};
  const NTSTATUS status = query(&attributes, &information);
  if (status < 0) {
    std::ostringstream message;
    message << "NtQueryFullAttributesFile failed: status=0x" << std::hex
            << std::uppercase << static_cast<std::uint32_t>(status);
    throw std::runtime_error(message.str());
  }
}

fs::path query_short_path(const fs::path &path) {
  std::wstring buffer(32768, L'\0');
  const DWORD length = GetShortPathNameW(
      path.c_str(), buffer.data(), static_cast<DWORD>(buffer.size()));
  if (length == 0)
    fail("GetShortPathNameW");
  if (length >= buffer.size())
    throw std::runtime_error("the short path exceeded the test buffer");
  buffer.resize(length);
  if (buffer.find(L'~') == std::wstring::npos) {
    throw std::runtime_error(
        "the VM volume did not assign an 8.3 alias to the tunnel probe");
  }
  return fs::path(std::move(buffer));
}

observations wait_for_observations(
    ntl::flt::communication_client &client) {
  const auto deadline =
      std::chrono::steady_clock::now() + std::chrono::seconds(5);
  observations current{};
  do {
    current = client.invoke(query_observations);
    if (current.reparses != 0 && current.network_disallowed != 0 &&
        current.destination_queries >= 2 &&
        current.renames_reissued != 0 && current.links_reissued != 0 &&
        current.tunnel_attempts != 0 && current.tunnel_successes != 0 &&
        current.tunnel_names_found != 0 &&
        current.tunnel_names_verified != 0 &&
        current.tunnel_states_created != 0 &&
        current.tunnel_states_created == current.tunnel_states_destroyed) {
      return current;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(25));
  } while (std::chrono::steady_clock::now() < deadline);
  return current;
}

void require_observations(const observations &value) {
  if (value.create_candidates == 0 || value.reparses == 0)
    throw std::runtime_error("create reparse was not observed");
  if (value.network_queries == 0 || value.network_disallowed == 0)
    throw std::runtime_error(
        "network-query-open Fast I/O fallback was not observed");
  if (value.destination_queries < 2 || value.renames_reissued == 0 ||
      value.links_reissued == 0) {
    throw std::runtime_error(
        "rename and hard-link destinations were not both reissued");
  }
  if (value.tunnel_attempts == 0 || value.tunnel_successes == 0 ||
      value.tunnel_names_found == 0 || value.tunnel_names_verified == 0 ||
      value.tunnel_states_created == 0 ||
      value.tunnel_states_created != value.tunnel_states_destroyed) {
    throw std::runtime_error(
        "tunneled-name completion-state lifecycle was not balanced");
  }
  if (value.last_reparse_status < 0 ||
      value.last_destination_status < 0 ||
      value.last_reissue_status < 0 || value.last_tunnel_status < 0) {
    throw std::runtime_error("a typed SimRep helper returned failure");
  }
}

void print_observations(const observations &value) {
  std::cout << "reparses=" << value.reparses
            << " network_disallowed=" << value.network_disallowed
            << " destination_queries=" << value.destination_queries
            << " renames_reissued=" << value.renames_reissued
            << " links_reissued=" << value.links_reissued
            << " tunnel_successes=" << value.tunnel_successes
            << " tunnel_names_found=" << value.tunnel_names_found
            << " tunnel_names_verified=" << value.tunnel_names_verified
            << " tunnel_states=" << value.tunnel_states_created << '/'
            << value.tunnel_states_destroyed << '\n';
}

} // namespace

int wmain(int argc, wchar_t **argv) {
  if (argc > 2) {
    std::cerr << "usage: crtsys_flt_simrep_runtime_test_app [volume-root]\n";
    return 2;
  }

  try {
    fs::path root =
        argc == 2 ? fs::path(argv[1]) : fs::current_path().root_path();
    root = root.root_path();
    if (root.empty())
      throw std::runtime_error("a volume root such as C:\\ is required");

    auto client = ntl::flt::communication_client::connect(port_name);
    const fs::path visible = root / relative_mapping_path(visible_mapping);
    const fs::path backing = root / relative_mapping_path(backing_mapping);
    const fs::path visible_payload = visible / payload_name;
    const fs::path backing_payload = backing / payload_name;
    const fs::path visible_created = visible / created_name;
    const fs::path backing_created = backing / created_name;
    const fs::path rename_source = backing / rename_source_name;
    const fs::path visible_renamed = visible / renamed_name;
    const fs::path backing_renamed = backing / renamed_name;
    const fs::path link_source = backing / link_source_name;
    const fs::path visible_linked = visible / linked_name;
    const fs::path backing_linked = backing / linked_name;
    const fs::path tunnel_original = backing / tunnel_original_name;

    std::error_code error;
    fs::remove_all(backing, error);
    error.clear();
    remove_physical_visible_root(client, visible, false);
    if (!fs::create_directories(backing, error) || error)
      throw std::runtime_error("failed to create the backing directory: " +
                               error.message());
    create_physical_visible_root(client, visible);

    write_text(backing_payload, "NTL SimRep payload\n");
    if (read_text(visible_payload) != "NTL SimRep payload\n")
      throw std::runtime_error("create reparse returned the wrong payload");

    write_text(visible_created, "created through SimRep\n");
    if (read_text(backing_created) != "created through SimRep\n")
      throw std::runtime_error(
          "the reparsed create did not reach the backing directory");

    query_full_attributes(visible_payload);

    write_text(rename_source, "renamed through SimRep\n");
    if (!MoveFileExW(rename_source.c_str(), visible_renamed.c_str(),
                     MOVEFILE_REPLACE_EXISTING)) {
      fail("MoveFileExW");
    }
    if (read_text(backing_renamed) != "renamed through SimRep\n")
      throw std::runtime_error(
          "the reissued rename did not reach the backing directory");

    write_text(link_source, "linked through SimRep\n");
    if (!CreateHardLinkW(visible_linked.c_str(), link_source.c_str(),
                         nullptr)) {
      fail("CreateHardLinkW");
    }
    if (read_text(backing_linked) != "linked through SimRep\n")
      throw std::runtime_error(
          "the reissued hard link did not reach the backing directory");

    write_text(tunnel_original, "first tunnel probe\n");
    const fs::path tunnel_short = query_short_path(tunnel_original);
    if (!DeleteFileW(tunnel_short.c_str()))
      fail("DeleteFileW(tunnel probe)");
    write_text(tunnel_short, "second tunnel probe\n");

    const observations observed = wait_for_observations(client);
    print_observations(observed);
    require_observations(observed);

    fs::remove_all(backing, error);
    if (error)
      throw std::runtime_error("failed to remove the backing directory: " +
                               error.message());
    remove_physical_visible_root(client, visible, true);

    std::cout << "NTL SimRep runtime test PASS\n";
    return 0;
  } catch (const std::exception &failure) {
    std::cerr << "NTL SimRep runtime test FAIL: " << failure.what() << '\n';
    return 1;
  }
}
