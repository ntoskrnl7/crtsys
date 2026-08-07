#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <windows.h>

#include <cstddef>
#include <cstdint>
#include <iostream>
#include <sstream>
#include <span>
#include <stdexcept>
#include <string>
#include <system_error>

#include <ntl/net/inspection/core>
#include <ntl/net/user/task>
#include <ntl/rpc/client>
#include <ntl/rpc/coroutine>
#include <ntl/wfp/all>

#include "content_filter_control.hpp"
#include "tcp_content_filter_contract.hpp"

namespace {

using flow_layer_v4 = wfp_tcp_content_filter::flow_layer_v4;
using flow_layer_v6 = wfp_tcp_content_filter::flow_layer_v6;
using stream_layer_v4 = wfp_tcp_content_filter::stream_layer_v4;
using stream_layer_v6 = wfp_tcp_content_filter::stream_layer_v6;
namespace control = crtsys::examples::wfp::content_filter::control;

ntl::rpc::client open_policy_client() {
  ntl::rpc::client client(wfp_tcp_content_filter::endpoint_name);
  if (!client)
    throw std::runtime_error("cannot open TCP content-filter RPC endpoint");
  ntl::rpc::contract_requirements requirements;
  requirements.contract_version(wfp_tcp_content_filter::contract_version)
      .transport_features(ntl::rpc::transport_features::asynchronous_calls |
                          ntl::rpc::transport_features::client_sessions |
                          ntl::rpc::transport_features::reliable_notifications)
      .capabilities(wfp_tcp_content_filter::capabilities::current)
      .notification(wfp_tcp_content_filter::inspection_requests)
      .method(wfp_tcp_content_filter::submit_verdict)
      .method(wfp_tcp_content_filter::query_stats);
  (void)client.require_contract(requirements);
  (void)client.start_session();
  client.subscribe(wfp_tcp_content_filter::inspection_requests);
  return client;
}

ntl::net::inspection::verdict
inspect_request(const wfp_tcp_content_request &request) {
  const auto bytes =
      std::as_bytes(std::span(request.frame.data(), request.frame.size()));
  if (request.content_offset !=
          wfp_tcp_content_filter::sample_u32_be_prefix_size ||
      (request.address_family != AF_INET &&
       request.address_family != AF_INET6) ||
      request.frame.size() <
          wfp_tcp_content_filter::sample_u32_be_prefix_size ||
      request.frame.size() > wfp_tcp_content_filter::maximum_frame_size ||
      request.content_size !=
          request.frame.size() -
              wfp_tcp_content_filter::sample_u32_be_prefix_size)
    return ntl::net::inspection::verdict::drop_flow;
  const std::uint32_t encoded_size =
      (static_cast<std::uint32_t>(request.frame[0]) << 24) |
      (static_cast<std::uint32_t>(request.frame[1]) << 16) |
      (static_cast<std::uint32_t>(request.frame[2]) << 8) |
      static_cast<std::uint32_t>(request.frame[3]);
  if (encoded_size != request.content_size)
    return ntl::net::inspection::verdict::drop_flow;
  ntl::net::inspection::context metadata{
      ntl::net::inspection::content_kind::opaque,
      ntl::net::inspection::direction::inbound,
      request.id,
      request.source_port,
      request.destination_port};
  const ntl::net::inspection::content_view frame(bytes);
  auto content = frame.subview(request.content_offset, request.content_size);
  if (!content)
    return ntl::net::inspection::verdict::drop_flow;
  const ntl::net::inspection::tcp_message_view message(metadata, frame,
                                                       *content);
  return ntl::net::inspection::evaluate(
      [](const ntl::net::inspection::tcp_message_view &value) {
        return crtsys::examples::wfp::content_filter::decide(
            value.content(), wfp_tcp_content_filter::maximum_record_body_size);
      },
      message);
}

struct policy_counts {
  std::uint32_t permitted = 0;
  std::uint32_t blocked = 0;
  std::uint32_t malformed = 0;
};

ntl::net::user::task<policy_counts>
run_normal_policy(ntl::rpc::client &client, std::size_t request_count) {
  policy_counts counts;
  for (std::size_t index = 0; index != request_count; ++index) {
    auto delivery = co_await client.receive_reliable_async(
        wfp_tcp_content_filter::inspection_requests);
    const auto verdict = inspect_request(delivery.payload());
    const auto wire =
        verdict == ntl::net::inspection::verdict::permit
            ? wfp_tcp_content_filter::wire_verdict::permit
        : verdict == ntl::net::inspection::verdict::block
            ? wfp_tcp_content_filter::wire_verdict::block
            : wfp_tcp_content_filter::wire_verdict::malformed;
    const std::int32_t status = client.invoke(
        wfp_tcp_content_filter::submit_verdict, delivery.payload().id,
        static_cast<std::uint8_t>(wire));
    if (status != STATUS_SUCCESS)
      throw std::runtime_error("TCP content verdict submission failed");
    client.acknowledge(wfp_tcp_content_filter::inspection_requests, delivery);
    if (wire == wfp_tcp_content_filter::wire_verdict::permit)
      ++counts.permitted;
    else if (wire == wfp_tcp_content_filter::wire_verdict::block)
      ++counts.blocked;
    else
      ++counts.malformed;
  }
  co_return counts;
}

void install_policy(ntl::wfp::policy_session &session,
                    std::uint16_t destination_port) {
  session.install([&](ntl::wfp::policy_transaction &transaction) {
    const auto provider = transaction.add_provider(
        {wfp_tcp_content_filter::provider_key,
         L"crtsys NTL WFP TCP content-filter provider",
         L"Ephemeral provider for framed inbound TCP inspection"});
    const auto sublayer = transaction.add_sublayer(
        provider, {wfp_tcp_content_filter::sublayer_key,
                   L"crtsys NTL WFP TCP content-filter sublayer",
                   L"Fail-closed user-mode TCP message inspection", 0x7600});
    const auto flow_callout_v4 = transaction.add_callout<flow_layer_v4>(
        provider, {wfp_tcp_content_filter::flow_callout_key_v4,
                   L"Attach inbound IPv4 TCP inspection state", L""});
    const auto stream_callout_v4 = transaction.add_callout<stream_layer_v4>(
        provider, {wfp_tcp_content_filter::stream_callout_key_v4,
                   L"Inspect complete IPv4 TCP messages", L""});
    const auto flow_callout_v6 = transaction.add_callout<flow_layer_v6>(
        provider, {wfp_tcp_content_filter::flow_callout_key_v6,
                   L"Attach inbound IPv6 TCP inspection state", L""});
    const auto stream_callout_v6 = transaction.add_callout<stream_layer_v6>(
        provider, {wfp_tcp_content_filter::stream_callout_key_v6,
                   L"Inspect complete IPv6 TCP messages", L""});
    ntl::wfp::arbitration_filter_builder<flow_layer_v4> flow_filter_v4(
        wfp_tcp_content_filter::flow_filter_key_v4,
        L"Select inbound IPv4 TCP server flows",
        ntl::wfp::callout_unavailable::block);
    flow_filter_v4.protocol_equal(IPPROTO_TCP)
        .direction_equal(FWP_DIRECTION_INBOUND)
        .local_port_equal(destination_port);
    transaction.add_arbitration_filter(sublayer, flow_callout_v4,
                                      flow_filter_v4);
    ntl::wfp::stream_filter_builder<stream_layer_v4> stream_filter_v4(
        wfp_tcp_content_filter::stream_filter_key_v4,
        L"Defer IPv4 TCP messages for a typed verdict",
        ntl::wfp::callout_unavailable::block);
    stream_filter_v4.local_port_equal(destination_port);
    transaction.add_stream_filter(sublayer, stream_callout_v4,
                                          stream_filter_v4);
    ntl::wfp::arbitration_filter_builder<flow_layer_v6> flow_filter_v6(
        wfp_tcp_content_filter::flow_filter_key_v6,
        L"Select inbound IPv6 TCP server flows",
        ntl::wfp::callout_unavailable::block);
    flow_filter_v6.protocol_equal(IPPROTO_TCP)
        .direction_equal(FWP_DIRECTION_INBOUND)
        .local_port_equal(destination_port);
    transaction.add_arbitration_filter(sublayer, flow_callout_v6,
                                      flow_filter_v6);
    ntl::wfp::stream_filter_builder<stream_layer_v6> stream_filter_v6(
        wfp_tcp_content_filter::stream_filter_key_v6,
        L"Defer IPv6 TCP messages for a typed verdict",
        ntl::wfp::callout_unavailable::block);
    stream_filter_v6.local_port_equal(destination_port);
    transaction.add_stream_filter(sublayer, stream_callout_v6,
                                          stream_filter_v6);
  });
}

std::string stats_text(const wfp_tcp_content_filter_stats &before,
                       const wfp_tcp_content_filter_stats &after,
                       const policy_counts &counts) {
  std::ostringstream output;
  output << "before.queued=" << before.queued << '\n'
         << "before.permitted=" << before.permitted << '\n'
         << "before.blocked=" << before.blocked << '\n'
         << "before.malformed=" << before.malformed << '\n'
         << "before.timed_out=" << before.timed_out << '\n'
         << "before.cancelled=" << before.cancelled << '\n'
         << "before.failed=" << before.failed << '\n'
         << "after.queued=" << after.queued << '\n'
         << "after.permitted=" << after.permitted << '\n'
         << "after.blocked=" << after.blocked << '\n'
         << "after.malformed=" << after.malformed << '\n'
         << "after.timed_out=" << after.timed_out << '\n'
         << "after.cancelled=" << after.cancelled << '\n'
         << "after.failed=" << after.failed << '\n'
         << "policy.permitted=" << counts.permitted << '\n'
         << "policy.blocked=" << counts.blocked << '\n'
         << "policy.malformed=" << counts.malformed << '\n';
  return output.str();
}

} // namespace

int wmain(int argc, wchar_t **argv) {
  try {
    const auto options = control::parse_options(argc, argv, true);
    auto client = open_policy_client();
    const auto before = client.invoke(wfp_tcp_content_filter::query_stats);
    auto policy = ntl::wfp::policy_session::ephemeral(
        L"crtsys TCP content-filter policy service");
    install_policy(policy, options.port);
    control::signal_ready(options);
    policy_counts counts{};
    if (options.behavior == L"normal") {
      counts = ntl::net::user::sync_wait(
          run_normal_policy(client, options.expected_requests));
      control::wait_for_stop(options);
    } else if (options.behavior == L"failure") {
      const auto first =
          client.receive_reliable(wfp_tcp_content_filter::inspection_requests);
      const auto second =
          client.receive_reliable(wfp_tcp_content_filter::inspection_requests);
      (void)client.invoke(wfp_tcp_content_filter::submit_verdict,
                          first.payload().id, std::uint8_t{0});
      ::Sleep(3500);
      (void)client.invoke(
          wfp_tcp_content_filter::submit_verdict, first.payload().id,
          static_cast<std::uint8_t>(
              wfp_tcp_content_filter::wire_verdict::permit));
      (void)client.invoke(
          wfp_tcp_content_filter::submit_verdict, second.payload().id,
          static_cast<std::uint8_t>(
              wfp_tcp_content_filter::wire_verdict::permit));
      client.acknowledge(wfp_tcp_content_filter::inspection_requests, first);
      client.acknowledge(wfp_tcp_content_filter::inspection_requests, second);
      const auto cancelled =
          client.receive_reliable(wfp_tcp_content_filter::inspection_requests);
      (void)cancelled;
      client.close_session();
      control::wait_for_stop(options);
      client = open_policy_client();
    } else {
      throw std::invalid_argument("unsupported TCP policy service behavior");
    }
    const auto after = client.invoke(wfp_tcp_content_filter::query_stats);
    control::write_file(options.stats_file,
                        stats_text(before, after, counts));
    client.unsubscribe(wfp_tcp_content_filter::inspection_requests);
    client.close_session();
    std::wcout << L"TCP content-filter policy service stopped: port="
               << options.port << L", behavior=" << options.behavior
               << L".\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "TCP content-filter policy service failed: " << error.what()
              << '\n';
    return 1;
  }
}
