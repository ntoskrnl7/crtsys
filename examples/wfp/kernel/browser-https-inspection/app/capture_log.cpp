#include "capture_log.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <fstream>
#include <iomanip>
#include <span>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>

namespace crtsys::wfp_kernel_browser_https {
namespace {

namespace contract = wfp_kernel_browser_https_inspection;

std::string protocol_name(contract::inspected_protocol value) {
  switch (value) {
  case contract::inspected_protocol::http1:
    return "http1";
  case contract::inspected_protocol::http2:
    return "http2";
  case contract::inspected_protocol::http3:
    return "http3";
  default:
    return "unknown";
  }
}

std::string action_name(contract::inspection_action value) {
  switch (value) {
  case contract::inspection_action::permitted:
    return "permit";
  case contract::inspection_action::blocked:
    return "block";
  case contract::inspection_action::failed:
    return "failed";
  default:
    return "none";
  }
}

std::string bounded_server_name(const contract::inspection_record &record) {
  const std::size_t size =
      (std::min)(static_cast<std::size_t>(record.server_name_size),
                 contract::maximum_server_name_size);
  return std::string(record.server_name.data(), size);
}

std::string safe_component(std::string_view value) {
  std::string result;
  result.reserve((std::min)(value.size(), std::size_t{80}));
  for (const unsigned char character : value) {
    if (result.size() == 80)
      break;
    result.push_back(std::isalnum(character) || character == '.' ||
                             character == '-'
                         ? static_cast<char>(character)
                         : '_');
  }
  return result.empty() ? "no-sni" : result;
}

void write_bytes(const std::filesystem::path &path,
                 std::span<const std::byte> bytes) {
  std::ofstream output(path, std::ios::binary | std::ios::trunc);
  if (!output)
    throw std::runtime_error("cannot create kernel browser capture");
  output.write(reinterpret_cast<const char *>(bytes.data()),
               static_cast<std::streamsize>(bytes.size()));
  if (!output)
    throw std::runtime_error("cannot write kernel browser capture");
}

} // namespace

capture_log::capture_log(std::filesystem::path root)
    : root_(std::filesystem::absolute(std::move(root))) {
  std::filesystem::create_directories(root_);
}

void capture_log::write(const contract::inspection_record &record) {
  if (record.request_size > record.request.size() ||
      record.response_size > record.response.size() ||
      record.server_name_size > contract::maximum_server_name_size)
    throw std::runtime_error("kernel browser capture ABI is invalid");

  std::ostringstream leaf;
  leaf << std::setfill('0') << std::setw(8) << record.sequence << '-'
       << safe_component(bounded_server_name(record)) << '-'
       << protocol_name(record.protocol) << '-' << action_name(record.action);
  const auto directory = root_ / leaf.str();
  std::filesystem::create_directories(directory);

  const auto response =
      std::span(record.response).first(record.response_size);
  if ((record.flags & contract::html_content) != 0)
    write_bytes(directory / "response.html", response);

  std::ofstream metadata(directory / "metadata.txt",
                         std::ios::binary | std::ios::trunc);
  if (!metadata)
    throw std::runtime_error("cannot create kernel browser metadata");
  metadata << "sequence=" << record.sequence << '\n'
           << "session-id=" << record.session_id << '\n'
           << "server-name=" << bounded_server_name(record) << '\n'
           << "protocol=" << protocol_name(record.protocol) << '\n'
           << "action=" << action_name(record.action) << '\n'
           << "status=" << record.status << '\n'
           << "failure-status=" << record.failure_status << '\n'
           << "request-metadata-bytes=" << record.request_size << '\n'
           << "response-bytes=" << record.response_size << '\n'
           << "original-family=" << record.original_family << '\n'
           << "original-port=" << record.original_port << '\n'
           << "request-transformed="
           << ((record.flags & contract::request_transformed) != 0) << '\n'
           << "response-transformed="
           << ((record.flags & contract::response_transformed) != 0) << '\n'
           << "compressed-content="
           << ((record.flags & contract::compressed_content) != 0) << '\n'
           << "html-content="
           << ((record.flags & contract::html_content) != 0) << '\n'
           << "extended-connect="
           << ((record.flags & contract::websocket_or_extended_connect) != 0)
           << '\n'
           << "datagram-or-webtransport="
           << ((record.flags & contract::datagram_or_webtransport) != 0)
           << '\n';
}

void capture_log::write_summary(const contract::service_info &before,
                                const contract::service_info &after,
                                std::uint64_t records,
                                std::uint64_t dropped) const {
  std::ofstream output(root_ / "summary.txt",
                       std::ios::binary | std::ios::trunc);
  if (!output)
    throw std::runtime_error("cannot create kernel browser summary");
  const auto delta = [](std::uint64_t first, std::uint64_t last) {
    return last >= first ? last - first : 0;
  };
  output << "records=" << records << '\n'
         << "reader-dropped=" << dropped << '\n'
         << "driver-dropped="
         << delta(before.capture_dropped, after.capture_dropped) << '\n'
         << "accepted=" << delta(before.accepted, after.accepted) << '\n'
         << "handshaken=" << delta(before.handshaken, after.handshaken) << '\n'
         << "origin-connected="
         << delta(before.origin_connected, after.origin_connected) << '\n'
         << "origin-completed="
         << delta(before.origin_completed, after.origin_completed) << '\n'
         << "permitted=" << delta(before.permitted, after.permitted) << '\n'
         << "blocked=" << delta(before.blocked, after.blocked) << '\n'
         << "transformed=" << delta(before.transformed, after.transformed)
         << '\n'
         << "failed=" << delta(before.failed, after.failed) << '\n'
         << "identity-requests="
         << delta(before.identity_requests, after.identity_requests) << '\n'
         << "identity-timeouts="
         << delta(before.identity_timeouts, after.identity_timeouts) << '\n'
         << "quic-v4-blocked="
         << delta(before.quic_gate.ipv4.block_decisions,
                  after.quic_gate.ipv4.block_decisions)
         << '\n'
         << "quic-v6-blocked="
         << delta(before.quic_gate.ipv6.block_decisions,
                  after.quic_gate.ipv6.block_decisions)
         << '\n'
         << "udp-outbound="
         << delta(before.quic_gate.translation.outbound_packets,
                  after.quic_gate.translation.outbound_packets)
         << '\n'
         << "udp-inbound="
         << delta(before.quic_gate.translation.inbound_packets,
                  after.quic_gate.translation.inbound_packets)
         << '\n'
         << "udp-mapping-updates="
         << delta(before.quic_gate.translation.mapping_updates,
                  after.quic_gate.translation.mapping_updates)
         << '\n'
         << "udp-mapping-misses="
         << delta(before.quic_gate.translation.mapping_misses,
                  after.quic_gate.translation.mapping_misses)
         << '\n'
         << "udp-injection-failures="
         << delta(before.quic_gate.translation.injection_failures,
                  after.quic_gate.translation.injection_failures)
         << '\n'
         << "udp-quota-rejections="
         << delta(before.quic_gate.translation.quota_rejections,
                  after.quic_gate.translation.quota_rejections)
         << '\n';
}

} // namespace crtsys::wfp_kernel_browser_https
