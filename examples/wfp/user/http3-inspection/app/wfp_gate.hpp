#pragma once

#include <windows.h>

#include <cstdint>
#include <filesystem>
#include <ntl/wfp/management>

#include "http3_wfp_gate_contract.hpp"

namespace crtsys::wfp_user_http3 {

struct gate_evidence {
  std::uint64_t ipv4_delta = 0;
  std::uint64_t ipv6_delta = 0;
  std::uint64_t application_hash = 0;
  std::uint32_t process_id = 0;
  std::uint16_t original_v4_port = 0;
  std::uint16_t original_v6_port = 0;
  unsigned gated_families = 0;
  unsigned direct_families = 0;
  unsigned unavailable_families = 0;
  unsigned webtransport_rejected_families = 0;
};

class gate_policy {
public:
  gate_policy(
      ntl::wfp::policy_session session,
      wfp_user_http3_inspection::gate_telemetry before,
      std::uint16_t port, bool unavailable) noexcept;
  gate_policy(const gate_policy &) = delete;
  gate_policy &operator=(const gate_policy &) = delete;
  gate_policy(gate_policy &&) noexcept = default;
  gate_policy &operator=(gate_policy &&) noexcept = default;

private:
  friend class gate_controller;
  ntl::wfp::policy_session session_;
  wfp_user_http3_inspection::gate_telemetry before_{};
  std::uint16_t port_ = 0;
  bool unavailable_ = false;
};

class gate_controller {
public:
  gate_controller(const std::filesystem::path &controlled_application,
                  std::uint32_t controlled_process_id);
  gate_controller(const gate_controller &) = delete;
  gate_controller &operator=(const gate_controller &) = delete;
  ~gate_controller();

  gate_policy install(std::uint16_t remote_port);
  gate_policy install_unavailable(std::uint16_t remote_port);

  void verify_gate(const gate_policy &policy, int family);
  void verify_unavailable(
      const gate_policy &policy, int family,
      std::uint64_t origin_hits);
  void record_webtransport_rejection(int family);

  wfp_user_http3_inspection::gate_telemetry snapshot() const;
  void verify_direct_after_removal(
      int family,
      const wfp_user_http3_inspection::gate_telemetry &before,
      const wfp_user_http3_inspection::gate_telemetry &after);
  gate_evidence evidence() const noexcept;

private:
  gate_policy install_policy(
      std::uint16_t remote_port, bool unavailable);

  HANDLE device_ = INVALID_HANDLE_VALUE;
  ntl::wfp::application_id application_;
  std::uint64_t application_hash_ = 0;
  std::uint32_t process_id_ = 0;
  std::uint64_t ipv4_delta_ = 0;
  std::uint64_t ipv6_delta_ = 0;
  std::uint16_t original_v4_port_ = 0;
  std::uint16_t original_v6_port_ = 0;
  unsigned gated_families_ = 0;
  unsigned direct_families_ = 0;
  unsigned unavailable_families_ = 0;
  unsigned webtransport_rejected_families_ = 0;
};

} // namespace crtsys::wfp_user_http3
