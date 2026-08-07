#pragma once

#include <filesystem>
#include <mutex>
#include <span>
#include <string>
#include <string_view>

#include "controller_protocol.hpp"

namespace crtsys::wfp_kernel_browser_https {

class acceptance_controller;

inline bool complete_http3_policy_evidence(
    const controller_protocol::http3_policy_evidence &value) noexcept {
  return value.http3_filter_id_v4 != 0 && value.http3_filter_id_v6 != 0 &&
         value.datagram_filter_id_v4 != 0 &&
         value.datagram_filter_id_v6 != 0 &&
         value.reverse_filter_id_v4 != 0 && value.reverse_filter_id_v6 != 0;
}

class remote_policy_scope {
public:
  remote_policy_scope() = default;
  remote_policy_scope(const remote_policy_scope &) = delete;
  remote_policy_scope &operator=(const remote_policy_scope &) = delete;
  remote_policy_scope(remote_policy_scope &&other) noexcept;
  remote_policy_scope &operator=(remote_policy_scope &&other) noexcept;
  ~remote_policy_scope();

  const controller_protocol::tcp_policy_evidence &tcp_evidence() const noexcept {
    return tcp_;
  }
  const controller_protocol::http3_policy_evidence &
  http3_evidence() const noexcept {
    return http3_;
  }

private:
  friend class acceptance_controller;
  explicit remote_policy_scope(
      acceptance_controller &owner,
      controller_protocol::tcp_policy_evidence evidence) noexcept;
  explicit remote_policy_scope(
      acceptance_controller &owner,
      controller_protocol::http3_policy_evidence evidence) noexcept;
  void reset() noexcept;

  acceptance_controller *owner_ = nullptr;
  controller_protocol::tcp_policy_evidence tcp_{};
  controller_protocol::http3_policy_evidence http3_{};
};

class acceptance_controller {
public:
  acceptance_controller(const std::filesystem::path &controller,
                        const std::filesystem::path &application);
  acceptance_controller(const acceptance_controller &) = delete;
  acceptance_controller &operator=(const acceptance_controller &) = delete;
  ~acceptance_controller();

  wfp_kernel_browser_https_inspection::service_info query_service();
  wfp_kernel_browser_https_inspection::inspection_read_result
  read_inspection(std::uint64_t cursor);
  wfp_kernel_browser_https_inspection::identity_request_read_result
  read_identity_request(std::uint64_t cursor);
  wfp_kernel_browser_https_inspection::quic_telemetry query_quic_telemetry();
  void configure_identity(
      const wfp_kernel_browser_https_inspection::certificate_config &identity);
  void configure_origin_security(
      std::string_view server_name,
      std::span<const std::byte> client_sha1_thumbprint,
      std::span<const std::byte> origin_leaf_der);
  void remove_origin_security(std::string_view server_name);
  void arm_origin_security_rollback_test();
  remote_policy_scope install_tcp_policy(std::uint16_t port_v4,
                                         std::uint16_t port_v6);
  remote_policy_scope install_http3_policy(std::uint16_t original_port);
  void clear_policy();
  void stop();

private:
  controller_protocol::response
  transact(controller_protocol::request request);
  void close_process() noexcept;

  void *pipe_ = reinterpret_cast<void *>(-1);
  void *process_ = nullptr;
  void *thread_ = nullptr;
  std::mutex mutex_;
  bool stopped_ = false;
};

inline auto query_service(acceptance_controller &controller) {
  return controller.query_service();
}
inline auto read_inspection(acceptance_controller &controller,
                            std::uint64_t cursor) {
  return controller.read_inspection(cursor);
}
inline auto read_identity_request(acceptance_controller &controller,
                                  std::uint64_t cursor) {
  return controller.read_identity_request(cursor);
}
inline auto query_quic_telemetry(acceptance_controller &controller) {
  return controller.query_quic_telemetry();
}
inline void configure_origin_security(
    acceptance_controller &controller, std::string_view server_name,
    std::span<const std::byte> client_sha1_thumbprint,
    std::span<const std::byte> origin_leaf_der) {
  controller.configure_origin_security(server_name, client_sha1_thumbprint,
                                       origin_leaf_der);
}
inline void remove_origin_security(acceptance_controller &controller,
                                   std::string_view server_name) {
  controller.remove_origin_security(server_name);
}
inline void arm_origin_security_rollback_test(
    acceptance_controller &controller) {
  controller.arm_origin_security_rollback_test();
}

} // namespace crtsys::wfp_kernel_browser_https
