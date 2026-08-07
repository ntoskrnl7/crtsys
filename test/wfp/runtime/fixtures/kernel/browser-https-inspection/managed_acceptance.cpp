#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <windows.h>

#include "managed_acceptance.hpp"
#include "acceptance_controller.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <future>
#include <iostream>
#include <limits>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include <ntl/net/grpc/framing>

#include "capture_log.hpp"
#include "certificate_authority.hpp"
#include "certificate_store.hpp"
#include "identity_provisioner.hpp"
#include "managed_tcp_scenario.hpp"
#include "managed_http3_evidence.hpp"
#include "managed_http3_client.hpp"
#include "managed_http3_origin.hpp"

namespace crtsys::wfp_kernel_browser_https {
namespace {

namespace contract = wfp_kernel_browser_https_inspection;

class origin_security_scope {
public:
  origin_security_scope(acceptance_controller &device,
                        std::string server_name,
                        std::span<const std::byte> client_thumbprint,
                        std::span<const std::byte> origin_leaf_der)
      : device_(device), server_name_(std::move(server_name)) {
    configure_origin_security(device_, server_name_, client_thumbprint,
                              origin_leaf_der);
    installed_ = true;
  }
  origin_security_scope(const origin_security_scope &) = delete;
  origin_security_scope &operator=(const origin_security_scope &) = delete;
  ~origin_security_scope() {
    if (!installed_)
      return;
    try {
      remove_origin_security(device_, server_name_);
    } catch (...) {
    }
  }
  void reset() {
    if (!installed_)
      return;
    remove_origin_security(device_, server_name_);
    installed_ = false;
  }

private:
  acceptance_controller &device_;
  std::string server_name_;
  bool installed_ = false;
};

void require_service_ready(const contract::service_info &service) {
  if (!service.process_id || !service.tcp_ready ||
      !service.workspace_lifetime_passed || !service.tcp_port_v4 ||
      !service.tcp_port_v6 ||
      service.identity_capacity != contract::identity_cache_capacity)
    throw std::runtime_error("kernel browser inspection service is not ready");
}

bool contains(std::span<const std::byte> bytes, std::string_view needle) {
  if (needle.empty() || bytes.size() < needle.size())
    return false;
  return std::string_view(
             reinterpret_cast<const char *>(bytes.data()), bytes.size())
             .find(needle) != std::string_view::npos;
}

bool grpc_payload_is(std::span<const std::byte> wire,
                     std::string_view expected) noexcept {
  const auto header = ntl::net::grpc::inspect_header(
      ntl::net::scatter_view::from_contiguous(wire), 4096);
  return header && !header->compressed &&
         wire.size() == 5 + header->payload_size &&
         header->payload_size == expected.size() &&
         std::equal(wire.begin() + 5, wire.end(),
                    reinterpret_cast<const std::byte *>(expected.data()));
}

managed_http3_acceptance_result require_managed_http3(
    acceptance_controller &device, const contract::service_info &before,
    capture_log &logger, std::uint64_t capture_baseline,
    managed_http3_origin &origin, std::uint16_t trigger_port) {
  const std::uint64_t origin_accepted_baseline = origin.accepted();
  const std::uint64_t origin_encoder_stream_baseline =
      origin.qpack_encoder_streams();
  const std::string authority =
      "localhost:" + std::to_string(origin.port());
  const auto request = [&](int family, std::string_view path, bool block) {
    std::cout << "[kernel-browser][h3] request " << path << ' '
              << (family == AF_INET ? "ipv4" : "ipv6") << std::endl;
    try {
      auto response = exchange_http3(family, trigger_port, path, block, true,
                                     authority);
      std::cout << "[kernel-browser][h3] request " << path << " complete"
                << std::endl;
      return response;
    } catch (const std::exception &error) {
      throw std::runtime_error(
          std::string("scenario=request path=") + std::string(path) +
          " family=" + (family == AF_INET ? "ipv4" : "ipv6") + " " +
          error.what());
    }
  };
  const auto allowed = request(AF_INET, "/allowed", false);
  const auto gzip = request(AF_INET, "/gzip", false);
  const auto deflate = request(AF_INET6, "/deflate", false);
  const auto brotli = request(AF_INET, "/br", false);
  const auto grpc = [&] {
    std::cout << "[kernel-browser][h3] grpc ipv6" << std::endl;
    try {
      auto response = exchange_http3_grpc(AF_INET6, trigger_port, authority);
      std::cout << "[kernel-browser][h3] grpc complete" << std::endl;
      return response;
    } catch (const std::exception &error) {
      throw std::runtime_error(std::string("scenario=grpc family=ipv6 ") +
                               error.what());
    }
  }();
  const auto multiplex = [&] {
    std::cout << "[kernel-browser][h3] multiplex ipv4" << std::endl;
    try {
      auto result = exercise_http3_multiplex(AF_INET, trigger_port, authority);
      std::cout << "[kernel-browser][h3] multiplex complete" << std::endl;
      return result;
    } catch (const std::exception &error) {
      throw std::runtime_error(std::string("scenario=multiplex family=ipv4 ") +
                               error.what());
    }
  }();
  const auto webtransport = [&] {
    std::cout << "[kernel-browser][h3] webtransport ipv6" << std::endl;
    try {
      auto result = exercise_webtransport(AF_INET6, trigger_port, authority);
      std::cout << "[kernel-browser][h3] webtransport complete" << std::endl;
      return result;
    } catch (const std::exception &error) {
      const auto diagnostic = query_service(device);
      throw std::runtime_error(
          std::string("scenario=webtransport family=ipv6 ") + error.what() +
          " rejection-stage=" +
          std::to_string(static_cast<std::uint32_t>(
              diagnostic.webtransport_last_rejection_stage)) +
          " rejection-status=" +
          std::to_string(diagnostic.webtransport_last_rejection_status) +
          " transform-action=" +
          std::to_string(diagnostic.webtransport_last_transform_action) +
          " transform-rule=" +
          std::to_string(diagnostic.webtransport_last_transform_rule) +
          " transform-before=" +
          std::to_string(diagnostic.webtransport_last_transform_before_flags) +
          " transform-after=" +
          std::to_string(diagnostic.webtransport_last_transform_after_flags) +
          " transform-body=" +
          std::to_string(diagnostic.webtransport_last_transform_body_size));
    }
  }();
  const auto blocked = request(AF_INET6, "/blocked", true);
  const auto negative_before = query_service(device);
  const std::uint64_t negative_origin_accepted = origin.accepted();
  const std::uint64_t negative_origin_requests = origin.requests();
  const std::uint64_t negative_origin_transformed =
      origin.transformed_requests();
  const std::uint64_t negative_origin_decoder_acknowledgements =
      origin.decoder_acknowledgements();
  const std::uint64_t negative_origin_qpack_encoder_streams =
      origin.qpack_encoder_streams();
  const auto negative = [&] {
    std::cout << "[kernel-browser][h3] negative ipv4" << std::endl;
    try {
      auto result = exercise_http3_negative_acceptance(
          AF_INET, trigger_port, authority);
      std::cout << "[kernel-browser][h3] negative complete" << std::endl;
      return result;
    } catch (const std::exception &error) {
      throw std::runtime_error(std::string("scenario=negative family=ipv4 ") +
                               error.what());
    }
  }();
  const auto negative_after = query_service(device);
  const bool webtransport_rejection_no_session =
      negative.webtransport_client_inactive &&
      negative_after.webtransport_sessions ==
          negative_before.webtransport_sessions &&
      negative_after.webtransport_bidirectional ==
          negative_before.webtransport_bidirectional &&
      negative_after.webtransport_unidirectional ==
          negative_before.webtransport_unidirectional &&
      negative_after.webtransport_datagrams ==
          negative_before.webtransport_datagrams &&
      negative_after.webtransport_capsules ==
          negative_before.webtransport_capsules &&
      negative_after.webtransport_resets ==
          negative_before.webtransport_resets;
  const bool unsupported_extended_connect_fail_closed =
      negative.websocket_extended_connect_rejected &&
      negative.unknown_extended_connect_rejected;
  const bool invalid_request_headers_fail_closed =
      negative.uppercase_header_rejected &&
      negative.connection_header_rejected && negative.bad_te_header_rejected;
  const bool negative_origin_isolation =
      negative_after.http3_origin_connected ==
          negative_before.http3_origin_connected &&
      negative_after.http3_origin_completed ==
          negative_before.http3_origin_completed &&
      negative_after.http3_origin_failed ==
          negative_before.http3_origin_failed &&
      negative_after.http3_origin_peer_validated ==
          negative_before.http3_origin_peer_validated &&
      negative_after.http3_origin_h3_negotiated ==
          negative_before.http3_origin_h3_negotiated &&
      negative_after.http3_origin_peer_settings ==
          negative_before.http3_origin_peer_settings &&
      negative_after.http3_origin_qpack_acknowledgements ==
          negative_before.http3_origin_qpack_acknowledgements &&
      negative_after.origin_fallback_attempted ==
          negative_before.origin_fallback_attempted &&
      negative_after.origin_fallback_succeeded ==
          negative_before.origin_fallback_succeeded &&
      negative_after.origin_fallback_h2 == negative_before.origin_fallback_h2 &&
      negative_after.origin_fallback_http1 ==
          negative_before.origin_fallback_http1 &&
      negative_after.origin_fallback_rejected ==
          negative_before.origin_fallback_rejected &&
      negative_after.http3_worker_requests ==
          negative_before.http3_worker_requests &&
      negative_after.http3_pending_requests ==
          negative_before.http3_pending_requests &&
      negative_after.http3_buffered_request_bytes ==
          negative_before.http3_buffered_request_bytes &&
      negative_after.http3_origin_allocation_bytes ==
          negative_before.http3_origin_allocation_bytes &&
      negative_after.http3_origin_peak_allocation_bytes ==
          negative_before.http3_origin_peak_allocation_bytes &&
      negative_after.http3_origin_allocation_quota_rejections ==
          negative_before.http3_origin_allocation_quota_rejections &&
      origin.accepted() == negative_origin_accepted &&
      origin.requests() == negative_origin_requests &&
      origin.transformed_requests() == negative_origin_transformed &&
      origin.decoder_acknowledgements() ==
          negative_origin_decoder_acknowledgements &&
      origin.qpack_encoder_streams() ==
          negative_origin_qpack_encoder_streams;
  if (!negative.webtransport_policy_rejected ||
      !webtransport_rejection_no_session ||
      !unsupported_extended_connect_fail_closed ||
      !invalid_request_headers_fail_closed || !negative_origin_isolation)
    throw std::runtime_error(
        "managed kernel HTTP/3 negative acceptance is incomplete");

  auto after = negative_after;
  const auto drain_deadline =
      std::chrono::steady_clock::now() + std::chrono::seconds(5);
  while (after.http3_active_connections != 0 &&
         std::chrono::steady_clock::now() < drain_deadline) {
    std::this_thread::sleep_for(std::chrono::milliseconds(25));
    after = query_service(device);
  }

  std::vector<std::string_view> missing;
  const auto require = [&missing](bool observed, std::string_view name) {
    if (!observed)
      missing.push_back(name);
  };
  require(allowed.status == 200, "allowed-status");
  require(gzip.status == 200, "gzip-status");
  require(deflate.status == 200, "deflate-status");
  require(brotli.status == 200, "brotli-status");
  require(grpc.status == 200, "grpc-status");
  require(blocked.status == 403, "blocked-status");
  require(contains(allowed.body, "controlled origin h3"), "allowed-origin");
  require(contains(gzip.body, "controlled origin h3"), "gzip-origin");
  require(contains(deflate.body, "controlled origin h3"), "deflate-origin");
  require(contains(brotli.body, "controlled origin h3"), "brotli-origin");
  require(contains(allowed.body, "inspected and transformed by ntl"),
          "allowed-transform");
  require(contains(blocked.body, "blocked by browser inspection policy"),
          "blocked-policy");
  require(grpc_payload_is(grpc.body,
                          "ntl-grpc-transform|request|response"),
          "grpc-transform");
  require(gzip.content_encoding == "gzip", "gzip-content-encoding");
  require(deflate.content_encoding == "deflate",
          "deflate-content-encoding");
  require(brotli.content_encoding == "br", "brotli-content-encoding");
  require(allowed.dynamic_qpack_acknowledged, "allowed-qpack-ack");
  require(gzip.dynamic_qpack_acknowledged, "gzip-qpack-ack");
  require(deflate.dynamic_qpack_acknowledged, "deflate-qpack-ack");
  require(brotli.dynamic_qpack_acknowledged, "brotli-qpack-ack");
  require(grpc.dynamic_qpack_acknowledged, "grpc-qpack-ack");
  require(blocked.dynamic_qpack_acknowledged, "blocked-qpack-ack");
  require(webtransport.connected, "webtransport-connected");
  require(multiplex.one_connection, "multiplex-one-connection");
  require(multiplex.peer_settings, "multiplex-peer-settings");
  require(multiplex.concurrent_streams, "multiplex-concurrent-streams");
  require(multiplex.reverse_completion, "multiplex-reverse-completion");
  require(multiplex.stream_local_block, "multiplex-stream-local-block");
  require(multiplex.stream_local_reset, "multiplex-stream-local-reset");
  require(multiplex.aggregate_quota, "multiplex-aggregate-quota");
  require(multiplex.transformed, "multiplex-transformed");
  require(multiplex.clean_drain, "multiplex-clean-drain");
  require(webtransport.settings, "webtransport-settings");
  require(webtransport.extended_connect, "webtransport-extended-connect");
  require(webtransport.bidirectional, "webtransport-bidirectional");
  require(webtransport.unidirectional, "webtransport-unidirectional");
  require(webtransport.datagram, "webtransport-datagram");
  require(webtransport.capsule, "webtransport-capsule");
  require(webtransport.capsules == 3, "webtransport-capsule-count");
  require(webtransport.reliable_reset, "webtransport-reliable-reset");
  require(after.http3_accepted >= before.http3_accepted + 8,
          "service-accepted");
  require(after.http3_permitted >= before.http3_permitted + 11,
          "service-permitted");
  require(after.http3_blocked >= before.http3_blocked + 2,
          "service-blocked");
  require(after.http3_origin_connected >= before.http3_origin_connected + 10,
          "origin-connected");
  require(after.http3_origin_completed >= before.http3_origin_completed + 10,
          "origin-completed");
  require(after.http3_origin_failed == before.http3_origin_failed,
          "origin-failed");
  require(after.http3_origin_peer_validated >=
              before.http3_origin_peer_validated + 10,
          "origin-peer-validated");
  require(after.http3_origin_security_ready != 0, "origin-security-ready");
  // Exactly ten requests are forwarded to the origin: five standalone
  // exchanges and five permitted multiplex streams.  Policy blocks and the
  // locally terminated WebTransport session must not consume origin workers.
  require(after.http3_worker_requests >= before.http3_worker_requests + 10,
          "worker-requests");
  require(after.http3_peak_pending_requests >= 4, "peak-pending-requests");
  require(after.http3_pending_requests == 0, "pending-requests-drained");
  require(after.http3_irql_violations == before.http3_irql_violations,
          "irql-violations");
  require(after.qpack_resumed >= before.qpack_resumed + 6,
          "qpack-resumed");
  require(after.gzip_responses >= before.gzip_responses + 1,
          "gzip-response-counter");
  require(after.deflate_responses >= before.deflate_responses + 1,
          "deflate-response-counter");
  require(after.brotli_responses >= before.brotli_responses + 1,
          "brotli-response-counter");
  require(after.webtransport_sessions >= before.webtransport_sessions + 1,
          "webtransport-session-counter");
  require(after.webtransport_bidirectional >=
              before.webtransport_bidirectional + 1,
          "webtransport-bidirectional-counter");
  require(after.webtransport_unidirectional >=
              before.webtransport_unidirectional + 1,
          "webtransport-unidirectional-counter");
  require(after.webtransport_datagrams >= before.webtransport_datagrams + 1,
          "webtransport-datagram-counter");
  require(after.webtransport_capsules >= before.webtransport_capsules + 3,
          "webtransport-capsule-counter");
  require(after.webtransport_resets >= before.webtransport_resets + 1,
          "webtransport-reset-counter");
  require(after.http3_active_connections == 0, "connections-drained");
  require(after.http3_canceled_streams >= before.http3_canceled_streams + 2,
          "canceled-streams");
  require(after.http3_buffer_quota_rejections >=
              before.http3_buffer_quota_rejections + 1,
          "buffer-quota-rejections");
  require(after.http3_buffered_request_bytes == 0,
          "buffered-request-bytes-drained");
  require(origin.requests() >= 10, "origin-requests");
  require(origin.transformed_requests() >= 10, "origin-transforms");
  require(origin.decoder_acknowledgements() >= 10,
          "origin-decoder-acknowledgements");
  require(origin.accepted() >= origin_accepted_baseline + 10,
          "origin-accepted");
  require(origin.qpack_encoder_streams() - origin_encoder_stream_baseline ==
              origin.accepted() - origin_accepted_baseline,
          "origin-single-qpack-encoder-stream");
  if (!missing.empty()) {
    std::string detail =
        "managed kernel browser HTTP/3 evidence is incomplete:";
    for (const auto name : missing) {
      detail.push_back(' ');
      detail.append(name);
    }
    detail += " accepted=" +
              std::to_string(after.http3_accepted - before.http3_accepted) +
              " permitted=" +
              std::to_string(after.http3_permitted - before.http3_permitted) +
              " blocked=" +
              std::to_string(after.http3_blocked - before.http3_blocked) +
              " origin-connected=" +
              std::to_string(after.http3_origin_connected -
                             before.http3_origin_connected) +
              " origin-completed=" +
              std::to_string(after.http3_origin_completed -
                             before.http3_origin_completed) +
              " origin-failed=" +
              std::to_string(after.http3_origin_failed -
                             before.http3_origin_failed) +
              " origin-peer-validated=" +
              std::to_string(after.http3_origin_peer_validated -
                             before.http3_origin_peer_validated) +
              " origin-last-status=" +
              std::to_string(after.origin_last_status) +
              " origin-last-kind=" +
              std::to_string(after.origin_last_failure_kind) +
              " origin-last-stage=" +
              std::to_string(static_cast<std::uint32_t>(
                  after.origin_last_failure_stage)) +
              " origin-last-fallback-phase=" +
              std::to_string(static_cast<std::uint32_t>(
                  after.origin_last_fallback_phase)) +
              " worker-requests=" +
              std::to_string(after.http3_worker_requests -
                             before.http3_worker_requests) +
              " completion-order=" + multiplex.completion_order +
              " canceled-streams=" +
              std::to_string(after.http3_canceled_streams -
                             before.http3_canceled_streams) +
              " peak-buffered-request-bytes=" +
              std::to_string(after.http3_peak_buffered_request_bytes) +
              " buffer-quota-rejections=" +
              std::to_string(after.http3_buffer_quota_rejections -
                             before.http3_buffer_quota_rejections) +
              " statuses=" + std::to_string(allowed.status) + "," +
              std::to_string(gzip.status) + "," +
              std::to_string(deflate.status) + "," +
              std::to_string(brotli.status) + "," +
              std::to_string(grpc.status) + "," +
              std::to_string(blocked.status) +
              " origin-requests=" + std::to_string(origin.requests()) +
              " origin-accepted=" + std::to_string(origin.accepted()) +
              " origin-transforms=" +
              std::to_string(origin.transformed_requests()) +
              " origin-decoder-acks=" +
              std::to_string(origin.decoder_acknowledgements());
    throw std::runtime_error(detail);
  }

  std::uint64_t cursor = capture_baseline;
  std::uint64_t h3_records = 0;
  std::uint64_t grpc_records = 0;
  for (;;) {
    const auto next = read_inspection(device, cursor);
    if (next.dropped != 0)
      throw std::runtime_error("managed HTTP/3 capture queue dropped records");
    if (!next.available)
      break;
    cursor = next.record.sequence;
    logger.write(next.record);
    if (next.record.protocol == contract::inspected_protocol::http3) {
      ++h3_records;
      if ((next.record.flags & contract::grpc_message) != 0) {
        if (next.record.action != contract::inspection_action::permitted)
          throw std::runtime_error(
              "managed HTTP/3 gRPC capture has the wrong action");
        ++grpc_records;
      }
    }
  }
  if (h3_records < 13 || grpc_records != 1)
    throw std::runtime_error("managed HTTP/3 capture records are incomplete");
  return {.policy_pipeline = true,
          .compression = true,
          .grpc = true,
          .dynamic_qpack = true,
          .inbound_peer_settings = multiplex.peer_settings,
          .origin_h3_negotiated =
              after.http3_origin_h3_negotiated >=
              before.http3_origin_h3_negotiated + 10,
          .origin_peer_settings =
              after.http3_origin_peer_settings >=
              before.http3_origin_peer_settings + 10,
          .origin_qpack_acknowledgement =
              after.http3_origin_qpack_acknowledgements >=
              before.http3_origin_qpack_acknowledgements + 10,
          .origin_single_qpack_encoder_stream =
              origin.qpack_encoder_streams() -
                  origin_encoder_stream_baseline ==
              origin.accepted() - origin_accepted_baseline,
          .webtransport = true,
          .capsule_stream = webtransport.capsules == 3,
          .multiplex = multiplex.concurrent_streams &&
                       after.http3_peak_pending_requests >= 4,
          .reverse_completion = multiplex.reverse_completion,
          .stream_local_block = multiplex.stream_local_block,
          .stream_local_reset = multiplex.stream_local_reset,
          .aggregate_quota = multiplex.aggregate_quota,
          .multiplex_clean_drain = multiplex.clean_drain &&
                                   after.http3_active_connections == 0,
          .origin_system_validation =
              after.http3_origin_peer_validated >=
              before.http3_origin_peer_validated + 5,
          .origin_exact_pin =
              after.http3_origin_peer_validated >=
              before.http3_origin_peer_validated + 5,
          .origin_mtls = origin.requests() >= 5,
          .webtransport_policy_rejected =
              negative.webtransport_policy_rejected,
          .webtransport_rejection_no_session =
              webtransport_rejection_no_session,
          .unsupported_extended_connect_fail_closed =
              unsupported_extended_connect_fail_closed,
          .invalid_request_headers_fail_closed =
              invalid_request_headers_fail_closed,
          .negative_origin_isolation = negative_origin_isolation,
          .capture_records = h3_records,
          .peak_pending_requests = after.http3_peak_pending_requests,
          .peak_buffered_request_bytes =
              after.http3_peak_buffered_request_bytes,
          .buffer_quota_rejections =
              after.http3_buffer_quota_rejections -
              before.http3_buffer_quota_rejections,
          .canceled_streams = after.http3_canceled_streams -
                              before.http3_canceled_streams};
}

bool require_http3_connection_churn(
    acceptance_controller &device, const contract::service_info &before,
    managed_http3_origin &origin, std::uint16_t trigger_port) {
  constexpr unsigned exchanges = 160;
  const std::string authority =
      "localhost:" + std::to_string(origin.port());
  for (unsigned index = 0; index != exchanges; ++index) {
    const int family = (index & 1u) == 0 ? AF_INET : AF_INET6;
    response client_response;
    try {
      client_response = exchange_http3(family, trigger_port, "/allowed",
                                       false, true, authority);
    } catch (const std::exception &error) {
      const auto diagnostic = query_service(device);
      const auto &layer = family == AF_INET ? diagnostic.quic_gate.ipv4
                                            : diagnostic.quic_gate.ipv6;
      const auto &translation = diagnostic.quic_gate.translation;
      throw std::runtime_error(
          std::string(error.what()) + " churn-index=" +
          std::to_string(index) + " family=" +
          (family == AF_INET ? "ipv4" : "ipv6") + " classify=" +
          std::to_string(layer.classify_hits) + " udp-outbound=" +
          std::to_string(translation.outbound_packets) + " udp-inbound=" +
          std::to_string(translation.inbound_packets) + " mappings=" +
          std::to_string(translation.mapping_updates) + " misses=" +
          std::to_string(translation.mapping_misses) +
          " injection-failures=" +
          std::to_string(translation.injection_failures) +
           " quota-rejections=" +
           std::to_string(translation.quota_rejections) +
           " h3-origin-connected=" +
           std::to_string(diagnostic.http3_origin_connected) +
           " h3-origin-completed=" +
           std::to_string(diagnostic.http3_origin_completed) +
           " h3-origin-failed=" +
           std::to_string(diagnostic.http3_origin_failed) +
           " h3-origin-validated=" +
           std::to_string(diagnostic.http3_origin_peer_validated) +
           " h3-origin-negotiated=" +
           std::to_string(diagnostic.http3_origin_h3_negotiated) +
           " h3-origin-settings=" +
           std::to_string(diagnostic.http3_origin_peer_settings) +
           " h3-origin-qpack-ack=" +
           std::to_string(
               diagnostic.http3_origin_qpack_acknowledgements) +
           " origin-last-status=" +
           std::to_string(static_cast<std::uint32_t>(
               diagnostic.origin_last_status)) +
           " origin-failure-kind=" +
           std::to_string(diagnostic.origin_last_failure_kind) +
           " origin-failure-stage=" +
           std::to_string(static_cast<std::uint32_t>(
               diagnostic.origin_last_failure_stage)) +
           " request-sink-status=" +
           std::to_string(static_cast<std::uint32_t>(
               diagnostic.http3_last_request_sink_status)) +
           " stream-rejection-status=" +
           std::to_string(static_cast<std::uint32_t>(
               diagnostic.http3_last_stream_rejection_status)) +
           " origin-submit-calls=" +
           std::to_string(diagnostic.http3_proxy_origin_submit_calls) +
           " origin-submit-status=" +
           std::to_string(static_cast<std::uint32_t>(
               diagnostic.http3_proxy_last_origin_submit_status)) +
           " baseline-v4-classify=" +
          std::to_string(before.quic_gate.ipv4.classify_hits) +
          " baseline-v6-classify=" +
          std::to_string(before.quic_gate.ipv6.classify_hits) +
          " baseline-outbound=" +
          std::to_string(before.quic_gate.translation.outbound_packets) +
          " baseline-inbound=" +
          std::to_string(before.quic_gate.translation.inbound_packets) +
          " baseline-mappings=" +
          std::to_string(before.quic_gate.translation.mapping_updates));
    }
    if (client_response.status != 200 ||
        !contains(client_response.body, "inspected and transformed by ntl"))
      throw std::runtime_error("HTTP/3 churn exchange failed");
  }
  contract::service_info after{};
  const auto deadline =
      std::chrono::steady_clock::now() + std::chrono::seconds(5);
  do {
    after = query_service(device);
    if (after.http3_reaped_connections >=
            before.http3_reaped_connections + exchanges - 1 &&
        after.http3_active_connections <= 1)
      break;
    std::this_thread::sleep_for(std::chrono::milliseconds(25));
  } while (std::chrono::steady_clock::now() < deadline);
  if (after.http3_accepted < before.http3_accepted + exchanges ||
      after.http3_origin_completed <
          before.http3_origin_completed + exchanges ||
      after.http3_origin_failed != before.http3_origin_failed ||
      after.http3_reaped_connections <
          before.http3_reaped_connections + exchanges - 1 ||
      after.http3_active_connections > 1 ||
      after.http3_peak_connections > 64 || origin.active_connections() > 2)
    throw std::runtime_error(
        "HTTP/3 connection quota was cumulative instead of concurrent");
  return true;
}

bool require_http3_origin_rejection(
    acceptance_controller &device,
    managed_http3_origin &origin,
    std::span<const std::byte> client_thumbprint,
    std::span<const std::byte> expected_origin_der,
    std::string_view case_name) {
  const std::uint16_t trigger_port = origin.port();
  origin_security_scope security(device, "localhost", client_thumbprint,
                                 expected_origin_der);
  const auto before = query_service(device);
  auto policy = device.install_http3_policy(trigger_port);
  const auto &evidence = policy.http3_evidence();
  if (!complete_http3_policy_evidence(evidence))
    throw std::runtime_error("negative HTTP/3 WFP redirect is missing");
  const std::string authority =
      "localhost:" + std::to_string(origin.port());
  contract::service_info in_flight{};
  bool captured_in_flight = false;
  response response_value;
  try {
    auto exchange = std::async(
        std::launch::async,
        [trigger_port, authority] {
          return exchange_http3(AF_INET, trigger_port, "/allowed",
                                false, true, authority);
        });
    if (exchange.wait_for(std::chrono::seconds(20)) ==
        std::future_status::timeout) {
      in_flight = query_service(device);
      captured_in_flight = true;
    }
    response_value = exchange.get();
  } catch (const std::exception &error) {
    const auto diagnostic = captured_in_flight ? in_flight
                                               : query_service(device);
    throw std::runtime_error(
        "HTTP/3 origin security negative exchange did not terminate: case=" +
        std::string(case_name) + " error=" + error.what() +
        " pending=" +
        std::to_string(diagnostic.http3_pending_requests) +
        " worker-requests-delta=" +
        std::to_string(diagnostic.http3_worker_requests -
                       before.http3_worker_requests) +
        " qpack-resumed=" + std::to_string(diagnostic.qpack_resumed) +
        "/" + std::to_string(before.qpack_resumed) +
        " request-calls=" +
        std::to_string(diagnostic.http3_proxy_request_stream_calls) +
        "/" + std::to_string(before.http3_proxy_request_stream_calls) +
        " request-finals=" +
        std::to_string(diagnostic.http3_proxy_request_stream_final_calls) +
        "/" +
        std::to_string(before.http3_proxy_request_stream_final_calls) +
        " request-headers=" +
        std::to_string(diagnostic.http3_proxy_request_headers) +
        "/" + std::to_string(before.http3_proxy_request_headers) +
        " request-ends=" +
        std::to_string(diagnostic.http3_proxy_request_stream_ends) +
        "/" + std::to_string(before.http3_proxy_request_stream_ends) +
        " inspector-retries=" +
        std::to_string(diagnostic.http3_proxy_request_inspector_retries) +
        "/" +
        std::to_string(before.http3_proxy_request_inspector_retries) +
        " origin-submit-calls=" +
        std::to_string(diagnostic.http3_proxy_origin_submit_calls) +
        "/" + std::to_string(before.http3_proxy_origin_submit_calls) +
        " active-connections=" +
        std::to_string(diagnostic.http3_active_connections) +
        " origin-connected-delta=" +
        std::to_string(diagnostic.http3_origin_connected -
                       before.http3_origin_connected) +
        " origin-completed-delta=" +
        std::to_string(diagnostic.http3_origin_completed -
                       before.http3_origin_completed) +
        " origin-failed-delta=" +
        std::to_string(diagnostic.http3_origin_failed -
                       before.http3_origin_failed) +
        " fallback-rejected-delta=" +
        std::to_string(diagnostic.origin_fallback_rejected -
                       before.origin_fallback_rejected) +
        " last-status=" + std::to_string(diagnostic.origin_last_status) +
        " last-kind=" +
        std::to_string(diagnostic.origin_last_failure_kind) +
        " last-stage=" +
        std::to_string(static_cast<std::uint32_t>(
            diagnostic.origin_last_failure_stage)) +
        " proxy-active=" +
        std::to_string(diagnostic.http3_proxy_active_requests) +
        " stream-end-status=" +
        std::to_string(diagnostic.http3_proxy_last_stream_end_status) +
        " stream-rejection-status=" +
        std::to_string(diagnostic.http3_last_stream_rejection_status) +
        " submit-status=" +
        std::to_string(diagnostic.http3_proxy_last_origin_submit_status));
  }
  const auto after = query_service(device);
  if (response_value.status != 502 ||
      !contains(response_value.body,
                "validated origin transport unavailable") ||
      after.http3_origin_failed < before.http3_origin_failed + 1 ||
      after.http3_origin_peer_validated !=
          before.http3_origin_peer_validated ||
      after.origin_fallback_attempted != before.origin_fallback_attempted ||
      after.origin_fallback_succeeded != before.origin_fallback_succeeded ||
      after.origin_fallback_h2 != before.origin_fallback_h2 ||
      after.origin_fallback_http1 != before.origin_fallback_http1 ||
      after.origin_fallback_rejected !=
          before.origin_fallback_rejected + 1)
    throw std::runtime_error("HTTP/3 origin security negative case failed: " +
                             std::string(case_name));
  return true;
}

} // namespace

int run_managed_acceptance(const std::filesystem::path &log_directory) {
  capture_log logger(log_directory);
  std::array<wchar_t, 32768> executable_path_buffer{};
  const DWORD executable_size = ::GetModuleFileNameW(
      nullptr, executable_path_buffer.data(),
      static_cast<DWORD>(executable_path_buffer.size()));
  if (executable_size == 0 ||
      executable_size == executable_path_buffer.size())
    throw std::system_error(::GetLastError(), std::system_category(),
                            "GetModuleFileNameW(managed acceptance)");
  const std::filesystem::path executable_path(
      std::wstring(executable_path_buffer.data(), executable_size));
  const auto controller_path =
      executable_path.parent_path() /
      L"crtsys_wfp_kernel_browser_https_inspection_controller.exe";
  acceptance_controller device(controller_path, executable_path);
  auto before = query_service(device);
  require_service_ready(before);
  ephemeral_authority authority;
  authority.export_public_certificate(logger.root() / "inspection-ca.cer");
  installed_certificate trusted_root(authority.get(), L"ROOT");
  ntl::net::windows_tls_certificate_issuer origin_issuer(
      authority.get(),
      {.key_name_prefix = L"crtsys-kernel-browser-controlled-origin",
       .rsa_bits = 2048,
       .validity_days = 2,
       .machine_keys = true});
  auto origin_leaf = origin_issuer.issue(L"localhost");
  installed_certificate origin_certificate(
      origin_leaf.borrowed_certificate(), L"MY");
  ntl::net::windows_tls_certificate_issuer client_issuer(
      authority.get(),
      {.key_name_prefix = L"crtsys-kernel-browser-controlled-client",
       .rsa_bits = 2048,
       .validity_days = 2,
       .machine_keys = true,
       .certificate_purpose =
           ntl::net::windows_tls_certificate_issuer_options::purpose::
               client_authentication});
  auto client_leaf = client_issuer.issue(L"ntl-managed-client");
  installed_certificate client_certificate(
      client_leaf.borrowed_certificate(), L"MY");
  auto secondary_origin_leaf =
      origin_issuer.issue(L"kernel-secondary.test");
  installed_certificate secondary_origin_certificate(
      secondary_origin_leaf.borrowed_certificate(), L"MY");
  managed_http3_origin origin(origin_certificate.thumbprint());
  // The intercepted tuple is the logical HTTPS origin. The application-
  // scoped FLOW_ESTABLISHED callout associates only the controlled client;
  // the kernel proxy's own origin flow reaches the broader packet-layer
  // filter without that context and is deliberately left untouched.
  const std::uint16_t http3_trigger_port = origin.port();
  identity_provisioner identities(
      [&device](const contract::certificate_config &identity) {
        device.configure_identity(identity);
      },
      authority.get(), logger.root());
  identities.ensure("localhost");
  identities.ensure("kernel-secondary.test");
  origin_security_scope origin_security(
      device, "localhost", client_certificate.thumbprint(),
      std::span<const std::byte>(
          reinterpret_cast<const std::byte *>(
              origin_leaf.borrowed_certificate()->pbCertEncoded),
          origin_leaf.borrowed_certificate()->cbCertEncoded));
  origin_security_scope secondary_origin_security(
      device, "kernel-secondary.test", client_certificate.thumbprint(),
      std::span<const std::byte>(
          reinterpret_cast<const std::byte *>(
              secondary_origin_leaf.borrowed_certificate()->pbCertEncoded),
          secondary_origin_leaf.borrowed_certificate()->cbCertEncoded));
  auto rollback_probe_leaf = origin_issuer.issue(L"localhost");
  arm_origin_security_rollback_test(device);
  bool origin_security_replace_failure_observed = false;
  try {
    configure_origin_security(
        device, "localhost", client_certificate.thumbprint(),
        std::span<const std::byte>(
            reinterpret_cast<const std::byte *>(
                rollback_probe_leaf.borrowed_certificate()->pbCertEncoded),
            rollback_probe_leaf.borrowed_certificate()->cbCertEncoded));
  } catch (const std::runtime_error &) {
    origin_security_replace_failure_observed = true;
  }
  if (!origin_security_replace_failure_observed)
    throw std::runtime_error(
        "managed origin security rollback fault was not observed");
  before = query_service(device);
  if (!before.http3_ready || !before.http3_port)
    throw std::runtime_error("managed kernel HTTP/3 listener is not ready");

  std::cout << "[kernel-browser] dynamic SNI" << std::endl;
  const auto dynamic_sni = run_dynamic_sni_acceptance(
      device, query_service(device),
      "kernel-secondary.test", secondary_origin_leaf.borrowed_certificate(),
      client_leaf.borrowed_certificate(),
      [&identities] { identities.replace("kernel-secondary.test"); });
  if (!dynamic_sni.second_name || !dynamic_sni.replacement ||
      !dynamic_sni.observed_peer_leaf_change ||
      !dynamic_sni.active_session_lifetime ||
      !dynamic_sni.unknown_name_failed_closed)
    throw std::runtime_error("managed dynamic SNI acceptance is incomplete");
  secondary_origin_security.reset();
  auto dynamic_sni_drained = query_service(device);
  const auto dynamic_sni_drain_deadline =
      std::chrono::steady_clock::now() + std::chrono::seconds(5);
  while (dynamic_sni_drained.active_tcp_sessions != 0 &&
         std::chrono::steady_clock::now() < dynamic_sni_drain_deadline) {
    std::this_thread::sleep_for(std::chrono::milliseconds(25));
    dynamic_sni_drained = query_service(device);
  }
  if (dynamic_sni_drained.active_tcp_sessions != 0)
    throw std::runtime_error("managed dynamic SNI sessions did not drain");
  const auto tcp_capture_snapshot =
      read_inspection(device,
                      (std::numeric_limits<std::uint64_t>::max)());
  const auto tcp_capture_cursor = tcp_capture_snapshot.current_sequence;
  std::cout << "[kernel-browser] TCP capture baseline="
            << tcp_capture_cursor << " oldest="
            << tcp_capture_snapshot.oldest_sequence << " active="
            << dynamic_sni_drained.active_tcp_sessions << std::endl;
  std::cout << "[kernel-browser] HTTP/1 + WebSocket + HTTP/2" << std::endl;
  const auto tcp_acceptance = run_managed_tcp_acceptance(
      device, query_service(device), logger,
      tcp_capture_cursor, origin_leaf.borrowed_certificate(),
      client_leaf.borrowed_certificate());
  if (!http1_evidence_complete(tcp_acceptance) ||
      !http2_evidence_complete(tcp_acceptance))
    throw std::runtime_error("managed kernel TCP acceptance is incomplete");
  std::cout << "[kernel-browser] HTTP/3 origin fallback" << std::endl;
  managed_http3_fallback_acceptance_result fallback_acceptance{};
  try {
    fallback_acceptance = run_managed_http3_fallback_acceptance(
        device, origin_leaf.borrowed_certificate(),
        client_leaf.borrowed_certificate());
  } catch (const std::exception &error) {
    const auto diagnostic = query_service(device);
    throw std::runtime_error(
        std::string(error.what()) +
        " fallback-origin-connected=" +
        std::to_string(diagnostic.http3_origin_connected) +
        " fallback-origin-completed=" +
        std::to_string(diagnostic.http3_origin_completed) +
        " fallback-origin-failed=" +
        std::to_string(diagnostic.http3_origin_failed) +
        " fallback-attempted=" +
        std::to_string(diagnostic.origin_fallback_attempted) +
        " fallback-succeeded=" +
        std::to_string(diagnostic.origin_fallback_succeeded) +
        " fallback-h2=" +
        std::to_string(diagnostic.origin_fallback_h2) +
        " fallback-h1=" +
        std::to_string(diagnostic.origin_fallback_http1) +
        " origin-last-status=" +
        std::to_string(static_cast<std::uint32_t>(
            diagnostic.origin_last_status)) +
        " stream-rejection-status=" +
        std::to_string(static_cast<std::uint32_t>(
            diagnostic.http3_last_stream_rejection_status)));
  }
  if (!fallback_acceptance.http2 || !fallback_acceptance.http1 ||
      !fallback_acceptance.non_safe_rejected)
    throw std::runtime_error(
        "managed kernel origin fallback acceptance is incomplete");
  before = query_service(device);
  const auto http3_capture_cursor =
      read_inspection(device,
                      (std::numeric_limits<std::uint64_t>::max)())
          .current_sequence;
  managed_http3_acceptance_result http3_acceptance{};
  bool udp_wfp_relay = false;
  bool connection_churn = false;
  {
    std::cout << "[kernel-browser] direct HTTP/3 origin baseline"
              << std::endl;
    require_http3_origin_handshake_direct(
        AF_INET, origin.port(), client_certificate.thumbprint());
    require_http3_origin_handshake_direct(
        AF_INET6, origin.port(), client_certificate.thumbprint());
    std::cout << "[kernel-browser] HTTP/3 policy + churn" << std::endl;
    auto managed_policy = device.install_http3_policy(http3_trigger_port);
    const auto &managed_policy_evidence = managed_policy.http3_evidence();
    if (!complete_http3_policy_evidence(managed_policy_evidence))
      throw std::runtime_error(
          "managed HTTP/3 WFP connectionless relay policy was not installed");
    try {
      http3_acceptance = require_managed_http3(
          device, before, logger, http3_capture_cursor, origin,
          http3_trigger_port);
    } catch (const std::exception &error) {
      const auto diagnostic = query_service(device);
      const auto &translation = diagnostic.quic_gate.translation;
      throw std::runtime_error(
          std::string(error.what()) + " v4-classify=" +
          std::to_string(diagnostic.quic_gate.ipv4.classify_hits) +
          " v6-classify=" +
          std::to_string(diagnostic.quic_gate.ipv6.classify_hits) +
          " udp-outbound=" +
          std::to_string(translation.outbound_packets) + " udp-inbound=" +
          std::to_string(translation.inbound_packets) + " mappings=" +
          std::to_string(translation.mapping_updates) + " misses=" +
          std::to_string(translation.mapping_misses) +
           " injection-failures=" +
           std::to_string(translation.injection_failures) +
           " quota-rejections=" +
           std::to_string(translation.quota_rejections) +
           " h3-origin-connected=" +
           std::to_string(diagnostic.http3_origin_connected) +
           " h3-origin-completed=" +
           std::to_string(diagnostic.http3_origin_completed) +
           " h3-origin-failed=" +
           std::to_string(diagnostic.http3_origin_failed) +
           " h3-origin-validated=" +
           std::to_string(diagnostic.http3_origin_peer_validated) +
           " h3-origin-negotiated=" +
           std::to_string(diagnostic.http3_origin_h3_negotiated) +
           " h3-origin-settings=" +
           std::to_string(diagnostic.http3_origin_peer_settings) +
           " h3-origin-qpack-ack=" +
           std::to_string(
               diagnostic.http3_origin_qpack_acknowledgements) +
           " origin-last-status=" +
           std::to_string(static_cast<std::uint32_t>(
               diagnostic.origin_last_status)) +
           " origin-failure-kind=" +
           std::to_string(diagnostic.origin_last_failure_kind) +
           " origin-failure-stage=" +
           std::to_string(static_cast<std::uint32_t>(
               diagnostic.origin_last_failure_stage)) +
           " request-sink-status=" +
           std::to_string(static_cast<std::uint32_t>(
               diagnostic.http3_last_request_sink_status)) +
           " stream-rejection-status=" +
           std::to_string(static_cast<std::uint32_t>(
               diagnostic.http3_last_stream_rejection_status)) +
           " origin-submit-calls=" +
           std::to_string(diagnostic.http3_proxy_origin_submit_calls) +
           " origin-submit-status=" +
           std::to_string(static_cast<std::uint32_t>(
               diagnostic.http3_proxy_last_origin_submit_status)) +
           " baseline-v4-classify=" +
          std::to_string(before.quic_gate.ipv4.classify_hits) +
          " baseline-v6-classify=" +
          std::to_string(before.quic_gate.ipv6.classify_hits) +
          " baseline-outbound=" +
          std::to_string(before.quic_gate.translation.outbound_packets) +
          " baseline-inbound=" +
          std::to_string(before.quic_gate.translation.inbound_packets) +
          " baseline-mappings=" +
          std::to_string(before.quic_gate.translation.mapping_updates));
    }
    if (!http3_evidence_complete(http3_acceptance))
      throw std::runtime_error(
          "managed kernel HTTP/3 result gate rejected the evidence");
    // The client trigger, kernel relay, and real origin use distinct tuples.
    // A complete exchange proves the WFP translation without allowing the
    // upstream QUIC connection to match the client-facing policy.
    udp_wfp_relay = true;
    connection_churn = require_http3_connection_churn(
        device, query_service(device), origin, http3_trigger_port);
  }
  origin_security.reset();

  std::cout << "[kernel-browser] HTTP/3 origin rejection" << std::endl;
  auto wrong_pin_leaf = origin_issuer.issue(L"localhost");
  const bool wrong_pin_rejected = require_http3_origin_rejection(
      device, origin, client_certificate.thumbprint(),
      std::span<const std::byte>(
          reinterpret_cast<const std::byte *>(
              wrong_pin_leaf.borrowed_certificate()->pbCertEncoded),
          wrong_pin_leaf.borrowed_certificate()->cbCertEncoded),
      "wrong-pin");

  ephemeral_authority untrusted_client_authority;
  ntl::net::windows_tls_certificate_issuer untrusted_client_issuer(
      untrusted_client_authority.get(),
      {.key_name_prefix = L"crtsys-kernel-browser-untrusted-client",
       .rsa_bits = 2048,
       .validity_days = 2,
       .machine_keys = true,
       .certificate_purpose =
           ntl::net::windows_tls_certificate_issuer_options::purpose::
               client_authentication});
  auto untrusted_client_leaf =
      untrusted_client_issuer.issue(L"ntl-untrusted-client");
  installed_certificate untrusted_client_certificate(
      untrusted_client_leaf.borrowed_certificate(), L"MY");
  const bool wrong_client_rejected = require_http3_origin_rejection(
      device, origin,
      untrusted_client_certificate.thumbprint(),
      std::span<const std::byte>(
          reinterpret_cast<const std::byte *>(
              origin_leaf.borrowed_certificate()->pbCertEncoded),
          origin_leaf.borrowed_certificate()->cbCertEncoded),
      "wrong-client");

  ephemeral_authority untrusted_origin_authority;
  ntl::net::windows_tls_certificate_issuer untrusted_origin_issuer(
      untrusted_origin_authority.get(),
      {.key_name_prefix = L"crtsys-kernel-browser-untrusted-origin",
       .rsa_bits = 2048,
       .validity_days = 2,
       .machine_keys = true});
  auto untrusted_origin_leaf = untrusted_origin_issuer.issue(L"localhost");
  installed_certificate untrusted_origin_certificate(
      untrusted_origin_leaf.borrowed_certificate(), L"MY");
  managed_http3_origin untrusted_origin(
      untrusted_origin_certificate.thumbprint());
  const bool unknown_ca_rejected = require_http3_origin_rejection(
      device, untrusted_origin,
      client_certificate.thumbprint(),
      std::span<const std::byte>(
          reinterpret_cast<const std::byte *>(
              untrusted_origin_leaf.borrowed_certificate()->pbCertEncoded),
          untrusted_origin_leaf.borrowed_certificate()->cbCertEncoded),
      "unknown-ca");

  auto final_service = query_service(device);
  const auto drain_deadline =
      std::chrono::steady_clock::now() + std::chrono::seconds(5);
  while ((final_service.active_tcp_sessions != 0 ||
          final_service.http3_active_connections != 0 ||
          origin.active_connections() != 0 ||
          untrusted_origin.active_connections() != 0) &&
         std::chrono::steady_clock::now() < drain_deadline) {
    std::this_thread::sleep_for(std::chrono::milliseconds(25));
    final_service = query_service(device);
  }
  const bool origin_security_removed =
      final_service.origin_security_ready == 0 &&
      final_service.http3_origin_security_ready == 0;
  const bool clean_drain = final_service.active_tcp_sessions == 0 &&
                           final_service.http3_active_connections == 0 &&
                           origin.active_connections() == 0 &&
                           untrusted_origin.active_connections() == 0;
  if (!origin_security_removed)
    throw std::runtime_error(
        "managed origin security was not removed after acceptance");
  if (!clean_drain)
    throw std::runtime_error(
        "managed kernel browser acceptance did not drain cleanly: "
        "tcp-sessions=" +
        std::to_string(final_service.active_tcp_sessions) +
        " h3-connections=" +
        std::to_string(final_service.http3_active_connections) +
        " h3-pending=" +
        std::to_string(final_service.http3_pending_requests) +
        " origin-connections=" +
        std::to_string(origin.active_connections()) +
        " untrusted-origin-connections=" +
        std::to_string(untrusted_origin.active_connections()));

  const bool origin_negative_cases = wrong_pin_rejected &&
                                     wrong_client_rejected &&
                                     unknown_ca_rejected;
  const bool permit_block = tcp_acceptance.http1_policy_pipeline &&
                            tcp_acceptance.http2_policy_pipeline &&
                            http3_acceptance.policy_pipeline;
  const bool origin_system_validation =
      tcp_acceptance.origin_system_validation &&
      http3_acceptance.origin_system_validation && unknown_ca_rejected;
  const bool origin_exact_pin = tcp_acceptance.origin_exact_pin &&
                                http3_acceptance.origin_exact_pin &&
                                wrong_pin_rejected;
  const bool origin_mtls = tcp_acceptance.origin_mtls &&
                           http3_acceptance.origin_mtls &&
                           wrong_client_rejected;
  const bool origin_security_replace_rollback =
      origin_security_replace_failure_observed &&
      tcp_acceptance.origin_exact_pin && http3_acceptance.origin_exact_pin &&
      tcp_acceptance.origin_mtls && http3_acceptance.origin_mtls;
  const bool all_observed =
      tcp_acceptance.http1_policy_pipeline &&
      tcp_acceptance.http1_pipelining && tcp_acceptance.http1_compression &&
      tcp_acceptance.http1_grpc && tcp_acceptance.http1_websocket &&
      tcp_acceptance.http1_ipv4_ipv6_wfp &&
      tcp_acceptance.http2_policy_pipeline &&
      tcp_acceptance.http2_multiplexing &&
      tcp_acceptance.http2_flow_control && tcp_acceptance.http2_goaway &&
      tcp_acceptance.http2_compression && tcp_acceptance.http2_grpc &&
      tcp_acceptance.http2_websocket &&
      tcp_acceptance.http2_extended_connect &&
      tcp_acceptance.http2_unsupported_connect_fail_closed &&
      tcp_acceptance.http2_ipv4_ipv6_wfp &&
      http3_acceptance.policy_pipeline && http3_acceptance.compression &&
      http3_acceptance.grpc &&
      http3_acceptance.dynamic_qpack &&
      http3_acceptance.inbound_peer_settings &&
      http3_acceptance.origin_h3_negotiated &&
      http3_acceptance.origin_peer_settings &&
      http3_acceptance.origin_qpack_acknowledgement &&
      http3_acceptance.origin_single_qpack_encoder_stream &&
      http3_acceptance.webtransport &&
      http3_acceptance.capsule_stream && http3_acceptance.multiplex &&
      http3_acceptance.reverse_completion &&
      http3_acceptance.stream_local_block &&
      http3_acceptance.stream_local_reset &&
      http3_acceptance.aggregate_quota &&
      http3_acceptance.multiplex_clean_drain &&
      http3_acceptance.webtransport_policy_rejected &&
      http3_acceptance.webtransport_rejection_no_session &&
      http3_acceptance.unsupported_extended_connect_fail_closed &&
      http3_acceptance.invalid_request_headers_fail_closed &&
      http3_acceptance.negative_origin_isolation && udp_wfp_relay &&
      dynamic_sni.second_name && dynamic_sni.replacement &&
      dynamic_sni.observed_peer_leaf_change &&
      dynamic_sni.active_session_lifetime &&
      dynamic_sni.unknown_name_failed_closed && origin_system_validation &&
      origin_exact_pin && origin_mtls &&
      origin_security_replace_rollback &&
      origin_negative_cases && fallback_acceptance.http2 &&
      fallback_acceptance.http1 && fallback_acceptance.non_safe_rejected &&
      connection_churn && permit_block &&
      clean_drain;
  if (!all_observed)
    throw std::runtime_error(
        "managed kernel browser observed evidence is incomplete");
  const auto status = [](bool value) noexcept {
    return value ? "pass" : "fail";
  };

  std::ofstream result(logger.root() / "managed-acceptance.json",
                       std::ios::binary | std::ios::trunc);
  if (!result)
    throw std::runtime_error("cannot create managed acceptance result");
  result << "{\n"
            "  \"schema\": \"ntl-kernel-browser-managed-acceptance-v3\",\n"
         << "  \"http1_policy_pipeline\": \""
         << status(tcp_acceptance.http1_policy_pipeline) << "\",\n"
         << "  \"http1_compression\": \""
         << status(tcp_acceptance.http1_compression) << "\",\n"
         << "  \"http1_grpc\": \""
         << status(tcp_acceptance.http1_grpc) << "\",\n"
         << "  \"http1_websocket\": \""
         << status(tcp_acceptance.http1_websocket) << "\",\n"
         << "  \"http1_ipv4_ipv6_wfp\": \""
         << status(tcp_acceptance.http1_ipv4_ipv6_wfp) << "\",\n"
         << "  \"http1_pipelining\": \""
         << status(tcp_acceptance.http1_pipelining) << "\",\n"
         << "  \"http2_policy_pipeline\": \""
         << status(tcp_acceptance.http2_policy_pipeline) << "\",\n"
         << "  \"http2_compression\": \""
         << status(tcp_acceptance.http2_compression) << "\",\n"
         << "  \"http2_grpc\": \""
         << status(tcp_acceptance.http2_grpc) << "\",\n"
         << "  \"http2_websocket\": \""
         << status(tcp_acceptance.http2_websocket) << "\",\n"
         << "  \"http2_extended_connect\": \""
         << status(tcp_acceptance.http2_extended_connect) << "\",\n"
         << "  \"http2_unsupported_connect_fail_closed\": \""
         << status(tcp_acceptance.http2_unsupported_connect_fail_closed)
         << "\",\n"
         << "  \"http2_ipv4_ipv6_wfp\": \""
         << status(tcp_acceptance.http2_ipv4_ipv6_wfp) << "\",\n"
         << "  \"http2_multiplexing\": \""
         << status(tcp_acceptance.http2_multiplexing) << "\",\n"
         << "  \"http2_flow_control\": \""
         << status(tcp_acceptance.http2_flow_control) << "\",\n"
         << "  \"http2_goaway\": \""
         << status(tcp_acceptance.http2_goaway) << "\",\n"
         << "  \"http3_policy_pipeline\": \""
         << status(http3_acceptance.policy_pipeline) << "\",\n"
         << "  \"http3_compression\": \""
         << status(http3_acceptance.compression) << "\",\n"
         << "  \"http3_grpc\": \""
         << status(http3_acceptance.grpc) << "\",\n"
         << "  \"http3_dynamic_qpack\": \""
         << status(http3_acceptance.dynamic_qpack) << "\",\n"
         << "  \"http3_inbound_peer_settings\": \""
         << status(http3_acceptance.inbound_peer_settings) << "\",\n"
         << "  \"http3_origin_h3_negotiated\": \""
         << status(http3_acceptance.origin_h3_negotiated) << "\",\n"
         << "  \"http3_origin_peer_settings\": \""
         << status(http3_acceptance.origin_peer_settings) << "\",\n"
         << "  \"http3_origin_qpack_acknowledgement\": \""
         << status(http3_acceptance.origin_qpack_acknowledgement) << "\",\n"
         << "  \"http3_origin_single_qpack_encoder_stream\": \""
         << status(http3_acceptance.origin_single_qpack_encoder_stream)
         << "\",\n"
         << "  \"http3_webtransport\": \""
         << status(http3_acceptance.webtransport) << "\",\n"
         << "  \"http3_capsule_stream\": \""
         << status(http3_acceptance.capsule_stream) << "\",\n"
         << "  \"http3_single_connection_multiplex\": \""
         << status(http3_acceptance.multiplex) << "\",\n"
         << "  \"http3_reverse_completion\": \""
         << status(http3_acceptance.reverse_completion) << "\",\n"
         << "  \"http3_stream_local_block\": \""
         << status(http3_acceptance.stream_local_block) << "\",\n"
         << "  \"http3_stream_local_reset\": \""
         << status(http3_acceptance.stream_local_reset) << "\",\n"
         << "  \"http3_aggregate_quota\": \""
         << status(http3_acceptance.aggregate_quota) << "\",\n"
         << "  \"http3_multiplex_clean_drain\": \""
         << status(http3_acceptance.multiplex_clean_drain) << "\",\n"
         << "  \"http3_webtransport_policy_rejected\": \""
         << status(http3_acceptance.webtransport_policy_rejected)
         << "\",\n"
         << "  \"http3_webtransport_rejection_no_session\": \""
         << status(http3_acceptance.webtransport_rejection_no_session)
         << "\",\n"
         << "  \"http3_unsupported_extended_connect_fail_closed\": \""
         << status(
                http3_acceptance.unsupported_extended_connect_fail_closed)
         << "\",\n"
         << "  \"http3_invalid_request_headers_fail_closed\": \""
         << status(http3_acceptance.invalid_request_headers_fail_closed)
         << "\",\n"
         << "  \"http3_negative_origin_isolation\": \""
         << status(http3_acceptance.negative_origin_isolation) << "\",\n"
         << "  \"tcp_wfp_redirect\": \""
         << status(tcp_acceptance.http1_ipv4_ipv6_wfp &&
                   tcp_acceptance.http2_ipv4_ipv6_wfp)
         << "\",\n"
         << "  \"udp_wfp_relay\": \""
         << status(udp_wfp_relay) << "\",\n"
         << "  \"dynamic_sni_second_name\": \""
         << status(dynamic_sni.second_name) << "\",\n"
         << "  \"identity_replacement_peer_leaf_changed\": \""
         << status(dynamic_sni.replacement &&
                   dynamic_sni.observed_peer_leaf_change)
         << "\",\n"
         << "  \"identity_replacement_active_session_lifetime\": \""
         << status(dynamic_sni.active_session_lifetime) << "\",\n"
         << "  \"unknown_sni_fail_closed\": \""
         << status(dynamic_sni.unknown_name_failed_closed) << "\",\n"
         << "  \"origin_system_validation\": \""
         << status(origin_system_validation) << "\",\n"
         << "  \"origin_exact_pin\": \"" << status(origin_exact_pin)
         << "\",\n"
         << "  \"origin_mtls\": \"" << status(origin_mtls)
         << "\",\n"
         << "  \"origin_security_replace_rollback\": \""
         << status(origin_security_replace_rollback) << "\",\n"
         << "  \"origin_wrong_pin_rejected\": \""
         << status(wrong_pin_rejected) << "\",\n"
         << "  \"origin_wrong_client_rejected\": \""
         << status(wrong_client_rejected) << "\",\n"
         << "  \"origin_unknown_ca_rejected\": \""
         << status(unknown_ca_rejected) << "\",\n"
         << "  \"origin_fallback_h2\": \""
         << status(fallback_acceptance.http2) << "\",\n"
         << "  \"origin_fallback_http1\": \""
         << status(fallback_acceptance.http1) << "\",\n"
         << "  \"origin_fallback_non_safe_rejected\": \""
         << status(fallback_acceptance.non_safe_rejected) << "\",\n"
         << "  \"origin_fallback_security_rejected\": \""
         << status(origin_negative_cases) << "\",\n"
         << "  \"connection_churn_over_quota\": \""
         << status(connection_churn) << "\",\n"
         << "  \"permit_block\": \"" << status(permit_block)
         << "\",\n"
         << "  \"clean_drain\": \"" << status(clean_drain) << "\",\n"
         << "  \"tcp_capture_records\": "
         << tcp_acceptance.capture_records << ",\n"
         << "  \"http3_capture_records\": "
         << http3_acceptance.capture_records << ",\n"
         << "  \"http3_peak_pending_requests\": "
         << http3_acceptance.peak_pending_requests << ",\n"
         << "  \"http3_peak_buffered_request_bytes\": "
         << http3_acceptance.peak_buffered_request_bytes << ",\n"
         << "  \"http3_buffer_quota_rejections\": "
         << http3_acceptance.buffer_quota_rejections << ",\n"
         << "  \"http3_canceled_streams\": "
         << http3_acceptance.canceled_streams << "\n"
         << "}\n";
  if (!result)
    throw std::runtime_error("cannot write managed acceptance result");
  result.close();
  if (!result)
    throw std::runtime_error("cannot flush managed acceptance result");
  device.stop();
  std::cout
      << "Kernel browser HTTPS inspection PASS: "
      << "workspace_lifetime=pass "
      << "http1_policy_pipeline="
      << status(tcp_acceptance.http1_policy_pipeline) << ' '
      << "http1_compression=" << status(tcp_acceptance.http1_compression)
      << ' ' << "http1_grpc=" << status(tcp_acceptance.http1_grpc)
      << ' ' << "http1_websocket="
      << status(tcp_acceptance.http1_websocket) << ' '
      << "http1_ipv4_ipv6_wfp="
      << status(tcp_acceptance.http1_ipv4_ipv6_wfp) << ' '
      << "http1_pipelining=" << status(tcp_acceptance.http1_pipelining)
      << ' ' << "http2_policy_pipeline="
      << status(tcp_acceptance.http2_policy_pipeline) << ' '
      << "http2_compression=" << status(tcp_acceptance.http2_compression)
      << ' ' << "http2_grpc=" << status(tcp_acceptance.http2_grpc)
      << ' ' << "http2_websocket="
      << status(tcp_acceptance.http2_websocket) << ' '
      << "http2_extended_connect="
      << status(tcp_acceptance.http2_extended_connect) << ' '
      << "http2_unsupported_connect=blocked" << ' '
      << "http2_ipv4_ipv6_wfp="
      << status(tcp_acceptance.http2_ipv4_ipv6_wfp) << ' '
      << "http2_multiplex=" << status(tcp_acceptance.http2_multiplexing)
      << ' ' << "http2_flow_control="
      << status(tcp_acceptance.http2_flow_control) << ' '
      << "http2_goaway=" << status(tcp_acceptance.http2_goaway) << ' '
      << "http3_policy_pipeline=" << status(http3_acceptance.policy_pipeline)
      << ' ' << "http3_compression="
      << status(http3_acceptance.compression) << ' '
      << "http3_grpc=" << status(http3_acceptance.grpc) << ' '
      << "http3_qpack=" << status(http3_acceptance.dynamic_qpack) << ' '
      << "http3_inbound_peer_settings="
      << status(http3_acceptance.inbound_peer_settings) << ' '
      << "http3_origin_h3_negotiated="
      << status(http3_acceptance.origin_h3_negotiated) << ' '
      << "http3_origin_peer_settings="
      << status(http3_acceptance.origin_peer_settings) << ' '
      << "http3_origin_qpack_acknowledgement="
      << status(http3_acceptance.origin_qpack_acknowledgement) << ' '
      << "http3_origin_single_qpack_encoder_stream="
      << status(http3_acceptance.origin_single_qpack_encoder_stream) << ' '
      << "http3_webtransport=" << status(http3_acceptance.webtransport)
      << ' ' << "http3_capsule_stream="
      << status(http3_acceptance.capsule_stream) << ' '
      << "http3_single_connection_multiplex="
      << status(http3_acceptance.multiplex) << ' '
      << "http3_reverse_completion="
      << status(http3_acceptance.reverse_completion) << ' '
      << "http3_stream_local_block="
      << status(http3_acceptance.stream_local_block) << ' '
      << "http3_stream_local_reset="
      << status(http3_acceptance.stream_local_reset) << ' '
      << "http3_aggregate_quota="
      << status(http3_acceptance.aggregate_quota) << ' '
      << "http3_multiplex_clean_drain="
      << status(http3_acceptance.multiplex_clean_drain) << ' '
      << "http3_webtransport_policy_rejected="
      << status(http3_acceptance.webtransport_policy_rejected) << ' '
      << "http3_webtransport_rejection_no_session="
      << status(http3_acceptance.webtransport_rejection_no_session) << ' '
      << "http3_unsupported_extended_connect_fail_closed="
      << status(http3_acceptance.unsupported_extended_connect_fail_closed)
      << ' ' << "http3_invalid_request_headers_fail_closed="
      << status(http3_acceptance.invalid_request_headers_fail_closed) << ' '
      << "http3_negative_origin_isolation="
      << status(http3_acceptance.negative_origin_isolation) << ' '
      << "tcp_wfp_redirect="
      << status(tcp_acceptance.http1_ipv4_ipv6_wfp &&
                tcp_acceptance.http2_ipv4_ipv6_wfp)
      << ' ' << "udp_wfp_relay=" << status(udp_wfp_relay) << ' '
      << "origin_system_validation=" << status(origin_system_validation)
      << ' ' << "origin_exact_pin=" << status(origin_exact_pin) << ' '
      << "origin_mtls=" << status(origin_mtls) << ' '
      << "origin_security_replace_rollback="
      << status(origin_security_replace_rollback) << ' '
      << "origin_negative_cases=" << status(origin_negative_cases) << ' '
      << "origin_fallback_h2=" << status(fallback_acceptance.http2) << ' '
      << "origin_fallback_http1=" << status(fallback_acceptance.http1) << ' '
      << "origin_fallback_non_safe=blocked" << ' '
      << "origin_fallback_security=blocked" << ' '
      << "dynamic_sni=" << status(dynamic_sni.second_name) << ' '
      << "identity_replacement="
      << status(dynamic_sni.replacement &&
                dynamic_sni.observed_peer_leaf_change &&
                dynamic_sni.active_session_lifetime)
      << ' ' << "unknown_sni_fail_closed="
      << status(dynamic_sni.unknown_name_failed_closed) << ' '
      << "churn_over_quota=" << status(connection_churn) << ' '
      << "permit_block=" << status(permit_block) << ' '
      << "clean_drain=" << status(clean_drain) << '\n';
  return 0;
}

} // namespace crtsys::wfp_kernel_browser_https
