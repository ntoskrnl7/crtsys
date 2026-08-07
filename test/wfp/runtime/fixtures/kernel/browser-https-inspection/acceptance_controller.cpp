#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include "acceptance_controller.hpp"

#include <algorithm>
#include <chrono>
#include <cstring>
#include <stdexcept>
#include <string>
#include <system_error>
#include <utility>

namespace crtsys::wfp_kernel_browser_https {
namespace {

namespace contract = wfp_kernel_browser_https_inspection;
namespace protocol = controller_protocol;

HANDLE as_handle(void *value) noexcept { return static_cast<HANDLE>(value); }

std::wstring quote_argument(std::wstring_view value) {
  std::wstring result(1, L'"');
  std::size_t backslashes = 0;
  for (const wchar_t character : value) {
    if (character == L'\\') {
      ++backslashes;
      continue;
    }
    if (character == L'"') {
      result.append(backslashes * 2 + 1, L'\\');
      result.push_back(character);
      backslashes = 0;
      continue;
    }
    result.append(backslashes, L'\\');
    backslashes = 0;
    result.push_back(character);
  }
  result.append(backslashes * 2, L'\\');
  result.push_back(L'"');
  return result;
}

void write_exact(HANDLE pipe, const void *buffer, std::size_t size) {
  const auto *next = static_cast<const std::byte *>(buffer);
  while (size != 0) {
    DWORD written = 0;
    if (!::WriteFile(pipe, next, static_cast<DWORD>(size), &written,
                     nullptr))
      throw std::system_error(::GetLastError(), std::system_category(),
                              "write acceptance control pipe");
    if (written == 0)
      throw std::runtime_error("acceptance control write made no progress");
    next += written;
    size -= written;
  }
}

void read_exact(HANDLE pipe, void *buffer, std::size_t size) {
  auto *next = static_cast<std::byte *>(buffer);
  while (size != 0) {
    DWORD read = 0;
    if (!::ReadFile(pipe, next, static_cast<DWORD>(size), &read, nullptr))
      throw std::system_error(::GetLastError(), std::system_category(),
                              "read acceptance control pipe");
    if (read == 0)
      throw std::runtime_error("acceptance controller closed unexpectedly");
    next += read;
    size -= read;
  }
}

} // namespace

remote_policy_scope::remote_policy_scope(
    acceptance_controller &owner,
    protocol::tcp_policy_evidence evidence) noexcept
    : owner_(&owner), tcp_(evidence) {}

remote_policy_scope::remote_policy_scope(
    acceptance_controller &owner,
    protocol::http3_policy_evidence evidence) noexcept
    : owner_(&owner), http3_(evidence) {}

remote_policy_scope::remote_policy_scope(remote_policy_scope &&other) noexcept
    : owner_(std::exchange(other.owner_, nullptr)), tcp_(other.tcp_),
      http3_(other.http3_) {}

remote_policy_scope &
remote_policy_scope::operator=(remote_policy_scope &&other) noexcept {
  if (this != &other) {
    reset();
    owner_ = std::exchange(other.owner_, nullptr);
    tcp_ = other.tcp_;
    http3_ = other.http3_;
  }
  return *this;
}

remote_policy_scope::~remote_policy_scope() { reset(); }

void remote_policy_scope::reset() noexcept {
  if (!owner_)
    return;
  try {
    owner_->clear_policy();
  } catch (...) {
  }
  owner_ = nullptr;
}

acceptance_controller::acceptance_controller(
    const std::filesystem::path &controller,
    const std::filesystem::path &application) {
  const auto controller_path = std::filesystem::canonical(controller);
  const auto application_path = std::filesystem::canonical(application);
  const auto pipe_name =
      L"\\\\.\\pipe\\crtsys-kernel-browser-" +
      std::to_wstring(::GetCurrentProcessId()) + L"-" +
      std::to_wstring(::GetTickCount64());
  std::wstring command_line = quote_argument(controller_path.wstring()) +
                              L" --control-server " +
                              quote_argument(pipe_name) + L" " +
                              quote_argument(application_path.wstring());
  STARTUPINFOW startup{};
  startup.cb = sizeof(startup);
  PROCESS_INFORMATION process{};
  if (!::CreateProcessW(controller_path.c_str(), command_line.data(), nullptr,
                        nullptr, FALSE, 0, nullptr,
                        controller_path.parent_path().c_str(), &startup,
                        &process))
    throw std::system_error(::GetLastError(), std::system_category(),
                            "launch kernel browser controller");
  process_ = process.hProcess;
  thread_ = process.hThread;

  const auto deadline = std::chrono::steady_clock::now() +
                        std::chrono::seconds(20);
  while (std::chrono::steady_clock::now() < deadline) {
    if (::WaitForSingleObject(as_handle(process_), 0) == WAIT_OBJECT_0) {
      DWORD exit_code = 0;
      (void)::GetExitCodeProcess(as_handle(process_), &exit_code);
      close_process();
      throw std::runtime_error(
          "kernel browser controller exited before ready: " +
          std::to_string(exit_code));
    }
    if (::WaitNamedPipeW(pipe_name.c_str(), 100)) {
      pipe_ = ::CreateFileW(pipe_name.c_str(), GENERIC_READ | GENERIC_WRITE,
                            0, nullptr, OPEN_EXISTING, 0, nullptr);
      if (as_handle(pipe_) != INVALID_HANDLE_VALUE)
        break;
    }
    const auto error = ::GetLastError();
    if (error != ERROR_FILE_NOT_FOUND && error != ERROR_PIPE_BUSY &&
        error != ERROR_SEM_TIMEOUT) {
      close_process();
      throw std::system_error(error, std::system_category(),
                              "connect kernel browser controller");
    }
  }
  if (as_handle(pipe_) == INVALID_HANDLE_VALUE) {
    close_process();
    throw std::runtime_error("kernel browser controller ready timed out");
  }
  protocol::request hello{};
  hello.code = protocol::operation::hello;
  (void)transact(hello);
}

acceptance_controller::~acceptance_controller() {
  try {
    stop();
  } catch (...) {
    if (as_handle(pipe_) != INVALID_HANDLE_VALUE) {
      (void)::CloseHandle(as_handle(pipe_));
      pipe_ = INVALID_HANDLE_VALUE;
    }
    close_process();
  }
}

protocol::response
acceptance_controller::transact(protocol::request request) {
  std::scoped_lock lock(mutex_);
  if (as_handle(pipe_) == INVALID_HANDLE_VALUE)
    throw std::runtime_error("kernel browser controller is not connected");
  request.protocol_version = protocol::version;
  request.size = sizeof(request);
  write_exact(as_handle(pipe_), &request, sizeof(request));
  protocol::response reply{};
  read_exact(as_handle(pipe_), &reply, sizeof(reply));
  if (reply.protocol_version != protocol::version ||
      reply.size != sizeof(reply) || reply.code != request.code)
    throw std::runtime_error("kernel browser controller reply mismatch");
  if (reply.failed)
    throw std::runtime_error(reply.error.data());
  return reply;
}

contract::service_info acceptance_controller::query_service() {
  protocol::request request{};
  request.code = protocol::operation::query_service;
  return transact(request).service;
}

contract::inspection_read_result
acceptance_controller::read_inspection(std::uint64_t cursor) {
  protocol::request request{};
  request.code = protocol::operation::read_inspection;
  request.cursor = cursor;
  return transact(request).inspection;
}

contract::identity_request_read_result
acceptance_controller::read_identity_request(std::uint64_t cursor) {
  protocol::request request{};
  request.code = protocol::operation::read_identity_request;
  request.cursor = cursor;
  return transact(request).identity_request;
}

contract::quic_telemetry acceptance_controller::query_quic_telemetry() {
  protocol::request request{};
  request.code = protocol::operation::query_quic_telemetry;
  return transact(request).quic;
}

void acceptance_controller::configure_identity(
    const contract::certificate_config &identity) {
  protocol::request request{};
  request.code = protocol::operation::configure_identity;
  request.identity = identity;
  (void)transact(request);
}

void acceptance_controller::configure_origin_security(
    std::string_view server_name,
    std::span<const std::byte> client_sha1_thumbprint,
    std::span<const std::byte> origin_leaf_der) {
  if (client_sha1_thumbprint.size() !=
          contract::certificate_thumbprint_size ||
      server_name.empty() ||
      server_name.size() > contract::maximum_server_name_size ||
      origin_leaf_der.empty() ||
      origin_leaf_der.size() > contract::maximum_certificate_der_size)
    throw std::invalid_argument("invalid acceptance origin security material");
  protocol::request request{};
  request.code = protocol::operation::configure_origin_security;
  request.origin_security.action = contract::origin_security_action::install;
  request.origin_security.server_name_size =
      static_cast<std::uint32_t>(server_name.size());
  std::copy(server_name.begin(), server_name.end(),
            request.origin_security.server_name.begin());
  std::copy(client_sha1_thumbprint.begin(), client_sha1_thumbprint.end(),
            request.origin_security.client_sha1_thumbprint.begin());
  request.origin_security.origin_leaf_der_size =
      static_cast<std::uint32_t>(origin_leaf_der.size());
  std::copy(origin_leaf_der.begin(), origin_leaf_der.end(),
            request.origin_security.origin_leaf_der.begin());
  (void)transact(request);
}

void acceptance_controller::remove_origin_security(
    std::string_view server_name) {
  if (server_name.empty() ||
      server_name.size() > contract::maximum_server_name_size)
    throw std::invalid_argument("invalid acceptance origin security host");
  protocol::request request{};
  request.code = protocol::operation::configure_origin_security;
  request.origin_security.action = contract::origin_security_action::remove;
  request.origin_security.server_name_size =
      static_cast<std::uint32_t>(server_name.size());
  std::copy(server_name.begin(), server_name.end(),
            request.origin_security.server_name.begin());
  (void)transact(request);
}

void acceptance_controller::arm_origin_security_rollback_test() {
  protocol::request request{};
  request.code = protocol::operation::arm_origin_security_rollback;
  (void)transact(request);
}

remote_policy_scope acceptance_controller::install_tcp_policy(
    std::uint16_t port_v4, std::uint16_t port_v6) {
  protocol::request request{};
  request.code = protocol::operation::install_tcp_policy;
  request.port_v4 = port_v4;
  request.port_v6 = port_v6;
  return remote_policy_scope(*this, transact(request).tcp_policy);
}

remote_policy_scope
acceptance_controller::install_http3_policy(std::uint16_t original_port) {
  protocol::request request{};
  request.code = protocol::operation::install_http3_policy;
  request.port_v4 = original_port;
  return remote_policy_scope(*this, transact(request).http3_policy);
}

void acceptance_controller::clear_policy() {
  protocol::request request{};
  request.code = protocol::operation::clear_policy;
  (void)transact(request);
}

void acceptance_controller::stop() {
  if (stopped_)
    return;
  protocol::request request{};
  request.code = protocol::operation::stop;
  (void)transact(request);
  stopped_ = true;
  (void)::CloseHandle(as_handle(pipe_));
  pipe_ = INVALID_HANDLE_VALUE;
  if (::WaitForSingleObject(as_handle(process_), 10000) != WAIT_OBJECT_0) {
    close_process();
    throw std::runtime_error("kernel browser controller stop timed out");
  }
  DWORD exit_code = 0;
  if (!::GetExitCodeProcess(as_handle(process_), &exit_code) ||
      exit_code != 0) {
    close_process();
    throw std::runtime_error("kernel browser controller stop failed");
  }
  close_process();
}

void acceptance_controller::close_process() noexcept {
  if (thread_) {
    (void)::CloseHandle(as_handle(thread_));
    thread_ = nullptr;
  }
  if (process_) {
    if (::WaitForSingleObject(as_handle(process_), 0) != WAIT_OBJECT_0) {
      (void)::TerminateProcess(as_handle(process_), ERROR_PROCESS_ABORTED);
      (void)::WaitForSingleObject(as_handle(process_), 5000);
    }
    (void)::CloseHandle(as_handle(process_));
    process_ = nullptr;
  }
}

} // namespace crtsys::wfp_kernel_browser_https
