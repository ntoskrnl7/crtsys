#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <windows.h>

#include <cstdint>
#include <iostream>
#include <sstream>
#include <stdexcept>

#include <ntl/wfp/all>

#include "runtime_controller.hpp"
#include "stream_edit_contract.hpp"

namespace {

namespace runtime = crtsys::examples::wfp::runtime;

void install_policy(ntl::wfp::policy_session &session, std::uint16_t port) {
  session.install([&](ntl::wfp::policy_transaction &transaction) {
    const auto provider = transaction.add_provider(
        {wfp_stream_edit::provider_key,
         L"crtsys NTL WFP stream-edit provider",
         L"Dynamic provider for one outbound TCP token replacement"});
    const auto sublayer = transaction.add_sublayer(
        provider, {wfp_stream_edit::sublayer_key,
                   L"crtsys NTL WFP stream-edit sublayer",
                   L"Flow association and stream-control filters", 0x7500});
    const auto flow_callout =
        transaction.add_callout<wfp_stream_edit::flow_layer>(
            provider, {wfp_stream_edit::flow_callout_key,
                       L"Attach stream editor flow state", L""});
    const auto stream_callout =
        transaction.add_callout<wfp_stream_edit::stream_layer>(
            provider, {wfp_stream_edit::stream_callout_key,
                       L"Replace selected outbound tokens", L""});

    ntl::wfp::inspection_filter_builder<wfp_stream_edit::flow_layer>
        flow_filter(wfp_stream_edit::flow_filter_key,
                    L"Attach state to the selected outbound TCP flow");
    flow_filter.protocol_equal(IPPROTO_TCP)
        .direction_equal(FWP_DIRECTION_OUTBOUND)
        .remote_port_equal(port);
    transaction.add_inspection_filter(sublayer, flow_callout, flow_filter);

    ntl::wfp::stream_filter_builder<wfp_stream_edit::stream_layer>
        stream_filter(wfp_stream_edit::stream_filter_key,
                      L"Replace bounded inline and OOB tokens",
                      ntl::wfp::callout_unavailable::block);
    stream_filter.remote_port_equal(port);
    transaction.add_stream_filter(sublayer, stream_callout,
                                          stream_filter);
  });
}

} // namespace

int wmain(int argc, wchar_t **argv) {
  try {
    runtime::arguments arguments(argc, argv);
    const auto port = arguments.required_port(L"--port");
    const auto lifecycle = runtime::parse_lifecycle(arguments);
    arguments.finish();

    auto policy = ntl::wfp::policy_session::ephemeral(
        L"crtsys ntl::wfp stream-edit controller");
    install_policy(policy, port);
    runtime::signal_ready(lifecycle);
    runtime::wait_for_stop(lifecycle);

    std::ostringstream stats;
    stats << "policy.port=" << port << '\n'
          << "policy.inline_edit=" << 1 << '\n'
          << "policy.oob_edit=" << 1 << '\n';
    runtime::write_file(lifecycle.stats_file, stats.str());
    std::wcout << L"Stream-edit controller stopped; ephemeral policy "
                  L"removed.\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "Stream-edit controller failed: " << error.what() << '\n';
    return 1;
  }
}
