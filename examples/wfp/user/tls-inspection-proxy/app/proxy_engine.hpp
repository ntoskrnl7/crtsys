#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>

#include <ntl/net/user/redirected_tls_inspection>

namespace crtsys::wfp_sample::tls_inspection {

enum class proxy_protocol : std::uint8_t { http1 = 1, http2 = 2 };
enum class proxy_action : std::uint8_t { permitted = 1, blocked = 2 };

struct proxy_connection_result {
  int address_family = AF_UNSPEC;
  std::uint16_t original_port = 0;
  std::uint64_t process_id = 0;
  std::size_t application_id_size = 0;
  std::wstring server_name;
  proxy_protocol protocol = proxy_protocol::http1;
  proxy_action action = proxy_action::blocked;
  bool request_transformed = false;
  bool response_transformed = false;
};

struct proxy_transform_observation {
  bool request_transformed = false;
  bool response_transformed = false;
};

/** Sample-only policy and diagnostics around the reusable NTL session. */
class proxy_policy_runtime {
public:
  proxy_policy_runtime();
  ~proxy_policy_runtime();
  proxy_policy_runtime(const proxy_policy_runtime &) = delete;
  proxy_policy_runtime &operator=(const proxy_policy_runtime &) = delete;

  std::shared_ptr<ntl::net::user::redirected_tls_http_dispatcher>
  dispatcher() const noexcept;
  proxy_transform_observation take(std::uint64_t connection_id) noexcept;

private:
  struct implementation;
  std::unique_ptr<implementation> implementation_;
};

proxy_connection_result make_proxy_connection_result(
    const ntl::net::user::redirected_tls_session_result &session,
    proxy_transform_observation observation) noexcept;

} // namespace crtsys::wfp_sample::tls_inspection
