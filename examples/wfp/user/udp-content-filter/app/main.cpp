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

#include <ntl/net/inspection/core>
#include <ntl/net/user/task>
#include <ntl/rpc/client>
#include <ntl/rpc/coroutine>
#include <ntl/wfp/all>

#include "content_filter_control.hpp"
#include "udp_content_filter_contract.hpp"

namespace {

using layer_v4 = wfp_udp_content_filter::layer_v4;
using layer_v6 = wfp_udp_content_filter::layer_v6;
namespace control = crtsys::examples::wfp::content_filter::control;

ntl::rpc::client open_policy_client() {
  ntl::rpc::client client(wfp_udp_content_filter::endpoint_name);
  if (!client)
    throw std::runtime_error("cannot open UDP content-filter RPC endpoint");
  ntl::rpc::contract_requirements requirements;
  requirements.contract_version(wfp_udp_content_filter::contract_version)
      .transport_features(ntl::rpc::transport_features::asynchronous_calls |
                          ntl::rpc::transport_features::client_sessions |
                          ntl::rpc::transport_features::reliable_notifications)
      .capabilities(wfp_udp_content_filter::capabilities::current)
      .notification(wfp_udp_content_filter::inspection_requests)
      .method(wfp_udp_content_filter::submit_verdict)
      .method(wfp_udp_content_filter::query_stats);
  (void)client.require_contract(requirements);
  (void)client.start_session();
  client.subscribe(wfp_udp_content_filter::inspection_requests);
  return client;
}

ntl::net::inspection::verdict
inspect_request(const wfp_udp_content_request &request) {
  if ((request.address_family != AF_INET &&
       request.address_family != AF_INET6) ||
      request.payload.empty() ||
      request.payload.size() > wfp_udp_content_filter::maximum_record_size)
    return ntl::net::inspection::verdict::drop_flow;
  ntl::net::inspection::context metadata{
      ntl::net::inspection::content_kind::opaque,
      ntl::net::inspection::direction::outbound,
      request.id,
      request.source_port,
      request.destination_port};
  const ntl::net::inspection::udp_datagram_view datagram(
      metadata, ntl::net::inspection::content_view(
                    std::as_bytes(std::span(request.payload))));
  return ntl::net::inspection::evaluate(
      [](const ntl::net::inspection::udp_datagram_view &value) {
        return crtsys::examples::wfp::content_filter::decide(
            value.payload(), wfp_udp_content_filter::maximum_record_body_size);
      },
      datagram);
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
        wfp_udp_content_filter::inspection_requests);
    const auto verdict = inspect_request(delivery.payload());
    const auto wire =
        verdict == ntl::net::inspection::verdict::permit
            ? wfp_udp_content_filter::wire_verdict::permit
        : verdict == ntl::net::inspection::verdict::block
            ? wfp_udp_content_filter::wire_verdict::block
            : wfp_udp_content_filter::wire_verdict::malformed;
    const std::int32_t status = client.invoke(
        wfp_udp_content_filter::submit_verdict, delivery.payload().id,
        static_cast<std::uint8_t>(wire));
    if (status != STATUS_SUCCESS)
      throw std::runtime_error("UDP content verdict submission failed");
    client.acknowledge(wfp_udp_content_filter::inspection_requests, delivery);
    if (wire == wfp_udp_content_filter::wire_verdict::permit)
      ++counts.permitted;
    else if (wire == wfp_udp_content_filter::wire_verdict::block)
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
        {wfp_udp_content_filter::provider_key,
         L"crtsys NTL WFP UDP content-filter provider",
         L"Ephemeral provider for complete outbound UDP decisions"});
    const auto sublayer = transaction.add_sublayer(
        provider, {wfp_udp_content_filter::sublayer_key,
                   L"crtsys NTL WFP UDP content-filter sublayer",
                   L"Fail-closed user-mode UDP datagram inspection", 0x7600});
    const auto callout_v4 = transaction.add_callout<layer_v4>(
        provider, {wfp_udp_content_filter::callout_key_v4,
                   L"Inspect complete outbound IPv4 UDP datagrams", L""});
    const auto callout_v6 = transaction.add_callout<layer_v6>(
        provider, {wfp_udp_content_filter::callout_key_v6,
                   L"Inspect complete outbound IPv6 UDP datagrams", L""});
    ntl::wfp::packet_filter_builder<layer_v4> filter_v4(
        wfp_udp_content_filter::filter_key_v4,
        L"Inspect IPv4 UDP datagrams sent to the selected port",
        ntl::wfp::callout_unavailable::block);
    filter_v4.protocol_equal(IPPROTO_UDP)
        .direction_equal(FWP_DIRECTION_OUTBOUND)
        .remote_port_equal(destination_port);
    transaction.add_packet_filter(sublayer, callout_v4, filter_v4);
    ntl::wfp::packet_filter_builder<layer_v6> filter_v6(
        wfp_udp_content_filter::filter_key_v6,
        L"Inspect IPv6 UDP datagrams sent to the selected port",
        ntl::wfp::callout_unavailable::block);
    filter_v6.protocol_equal(IPPROTO_UDP)
        .direction_equal(FWP_DIRECTION_OUTBOUND)
        .remote_port_equal(destination_port);
    transaction.add_packet_filter(sublayer, callout_v6, filter_v6);
  });
}

std::string stats_text(const wfp_udp_content_filter_stats &before,
                       const wfp_udp_content_filter_stats &after,
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
         << "after.injection_completion_failures="
         << after.injection_completion_failures << '\n'
         << "after.last_injection_status=" << after.last_injection_status
         << '\n'
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
    const auto before = client.invoke(wfp_udp_content_filter::query_stats);
    auto policy = ntl::wfp::policy_session::ephemeral(
        L"crtsys UDP content-filter policy service");
    install_policy(policy, options.port);
    control::signal_ready(options);
    policy_counts counts{};
    if (options.behavior == L"normal") {
      counts = ntl::net::user::sync_wait(
          run_normal_policy(client, options.expected_requests));
      control::wait_for_stop(options);
    } else if (options.behavior == L"failure") {
      const auto first =
          client.receive_reliable(wfp_udp_content_filter::inspection_requests);
      (void)client.invoke(wfp_udp_content_filter::submit_verdict,
                          first.payload().id, std::uint8_t{0});
      ::Sleep(3500);
      (void)client.invoke(
          wfp_udp_content_filter::submit_verdict, first.payload().id,
          static_cast<std::uint8_t>(
              wfp_udp_content_filter::wire_verdict::permit));
      client.acknowledge(wfp_udp_content_filter::inspection_requests, first);
      client.close_session();
      client = open_policy_client();
      const auto cancelled =
          client.receive_reliable(wfp_udp_content_filter::inspection_requests);
      (void)cancelled;
      client.close_session();
      control::wait_for_stop(options);
      client = open_policy_client();
    } else {
      throw std::invalid_argument("unsupported UDP policy service behavior");
    }
    const auto after = client.invoke(wfp_udp_content_filter::query_stats);
    control::write_file(options.stats_file,
                        stats_text(before, after, counts));
    client.unsubscribe(wfp_udp_content_filter::inspection_requests);
    client.close_session();
    std::wcout << L"UDP content-filter policy service stopped: port="
               << options.port << L", behavior=" << options.behavior
               << L".\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "UDP content-filter policy service failed: " << error.what()
              << '\n';
    return 1;
  }
}
