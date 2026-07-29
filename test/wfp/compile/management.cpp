#include <cstdint>
#include <type_traits>

#include <ntl/wfp/all>

namespace {

using connect_layer = ntl::wfp::layers::ale_auth_connect_v4;
using redirect_layer =
    ntl::wfp::layers::ale_connect_redirect_v4;
using redirect_layer_v6 =
    ntl::wfp::layers::ale_connect_redirect_v6;
using bind_redirect_layer =
    ntl::wfp::layers::ale_bind_redirect_v4;
using flow_layer = ntl::wfp::layers::ale_flow_established_v4;
using stream_layer = ntl::wfp::layers::stream_v4;
using datagram_layer = ntl::wfp::layers::datagram_data_v4;

constexpr GUID provider_guid = {
    0x73aed1a0, 0x5d33, 0x429d, {0xa7, 0x7e, 0x4c, 0x93, 0xe6, 0xf0, 0xaa, 0x01}};
constexpr GUID sublayer_guid = {
    0x73aed1a1, 0x5d33, 0x429d, {0xa7, 0x7e, 0x4c, 0x93, 0xe6, 0xf0, 0xaa, 0x01}};
constexpr GUID connect_callout_guid = {
    0x73aed1a2, 0x5d33, 0x429d, {0xa7, 0x7e, 0x4c, 0x93, 0xe6, 0xf0, 0xaa, 0x01}};
constexpr GUID connect_filter_guid = {
    0x73aed1a3, 0x5d33, 0x429d, {0xa7, 0x7e, 0x4c, 0x93, 0xe6, 0xf0, 0xaa, 0x01}};
constexpr GUID flow_callout_guid = {
    0x73aed1a4, 0x5d33, 0x429d, {0xa7, 0x7e, 0x4c, 0x93, 0xe6, 0xf0, 0xaa, 0x01}};
constexpr GUID flow_filter_guid = {
    0x73aed1a5, 0x5d33, 0x429d, {0xa7, 0x7e, 0x4c, 0x93, 0xe6, 0xf0, 0xaa, 0x01}};
constexpr GUID stream_callout_guid = {
    0x73aed1a6, 0x5d33, 0x429d, {0xa7, 0x7e, 0x4c, 0x93, 0xe6, 0xf0, 0xaa, 0x01}};
constexpr GUID stream_filter_guid = {
    0x73aed1a7, 0x5d33, 0x429d, {0xa7, 0x7e, 0x4c, 0x93, 0xe6, 0xf0, 0xaa, 0x01}};
constexpr GUID stream_control_filter_guid = {
    0x73aed1a8, 0x5d33, 0x429d, {0xa7, 0x7e, 0x4c, 0x93, 0xe6, 0xf0, 0xaa, 0x01}};
constexpr GUID datagram_callout_guid = {
    0x73aed1a9, 0x5d33, 0x429d, {0xa7, 0x7e, 0x4c, 0x93, 0xe6, 0xf0, 0xaa, 0x01}};
constexpr GUID datagram_filter_guid = {
    0x73aed1aa, 0x5d33, 0x429d, {0xa7, 0x7e, 0x4c, 0x93, 0xe6, 0xf0, 0xaa, 0x01}};
constexpr GUID redirect_callout_guid = {
    0x73aed1ab, 0x5d33, 0x429d, {0xa7, 0x7e, 0x4c, 0x93, 0xe6, 0xf0, 0xaa, 0x01}};
constexpr GUID redirect_filter_guid = {
    0x73aed1ac, 0x5d33, 0x429d, {0xa7, 0x7e, 0x4c, 0x93, 0xe6, 0xf0, 0xaa, 0x01}};
constexpr GUID bind_redirect_callout_guid = {
    0x73aed1ad, 0x5d33, 0x429d, {0xa7, 0x7e, 0x4c, 0x93, 0xe6, 0xf0, 0xaa, 0x01}};
constexpr GUID bind_redirect_filter_guid = {
    0x73aed1ae, 0x5d33, 0x429d, {0xa7, 0x7e, 0x4c, 0x93, 0xe6, 0xf0, 0xaa, 0x01}};

constexpr ntl::wfp::provider_key provider_key(provider_guid);
constexpr ntl::wfp::sublayer_key sublayer_key(sublayer_guid);
constexpr ntl::wfp::callout_key<connect_layer> connect_callout_key(
    connect_callout_guid);
constexpr ntl::wfp::filter_key<connect_layer> connect_filter_key(
    connect_filter_guid);
constexpr ntl::wfp::callout_key<flow_layer> flow_callout_key(
    flow_callout_guid);
constexpr ntl::wfp::filter_key<flow_layer> flow_filter_key(flow_filter_guid);
constexpr ntl::wfp::callout_key<stream_layer> stream_callout_key(
    stream_callout_guid);
constexpr ntl::wfp::filter_key<stream_layer> stream_filter_key(
    stream_filter_guid);
constexpr ntl::wfp::filter_key<stream_layer> stream_control_filter_key(
    stream_control_filter_guid);
constexpr ntl::wfp::callout_key<datagram_layer> datagram_callout_key(
    datagram_callout_guid);
constexpr ntl::wfp::filter_key<datagram_layer> datagram_filter_key(
    datagram_filter_guid);
constexpr ntl::wfp::callout_key<redirect_layer>
    redirect_callout_key(redirect_callout_guid);
constexpr ntl::wfp::filter_key<redirect_layer>
    redirect_filter_key(redirect_filter_guid);
constexpr ntl::wfp::callout_key<bind_redirect_layer>
    bind_redirect_callout_key(bind_redirect_callout_guid);
constexpr ntl::wfp::filter_key<bind_redirect_layer>
    bind_redirect_filter_key(bind_redirect_filter_guid);

static_assert(!std::is_convertible_v<ntl::wfp::provider_key, GUID>);
static_assert(!std::is_constructible_v<
              ntl::wfp::filter_key<connect_layer>,
              ntl::wfp::filter_key<flow_layer>>);
static_assert(!std::is_copy_constructible_v<ntl::wfp::dynamic_session>);
static_assert(!std::is_copy_constructible_v<
              ntl::wfp::redirected_connection>);
static_assert(
    ntl::wfp::local_proxy_target::from_filter_context(
        ntl::wfp::local_proxy_target{1234, 8080}.filter_context())
            .process_id == 1234);
static_assert(
    ntl::wfp::local_proxy_target::from_filter_context(
        ntl::wfp::local_proxy_target{1234, 8080}.filter_context())
        .port == 8080);
static_assert(
    ntl::wfp::local_proxy_target::from_filter_context(
        ntl::wfp::local_proxy_target{
            1234, 8080,
            ntl::wfp::original_destination_context::omit}
            .filter_context())
        .process_id == 1234);
static_assert(
    ntl::wfp::local_proxy_target::from_filter_context(
        ntl::wfp::local_proxy_target{
            1234, 8080,
            ntl::wfp::original_destination_context::omit}
            .filter_context())
        .context ==
    ntl::wfp::original_destination_context::omit);
static_assert(std::is_constructible_v<
              ntl::wfp::connect_redirect_filter_builder<
                  redirect_layer_v6>,
              ntl::wfp::filter_key<redirect_layer_v6>,
              std::wstring, ntl::wfp::local_proxy_target>);
static_assert(std::is_constructible_v<
              ntl::wfp::bind_redirect_filter_builder<
                  ntl::wfp::layers::ale_bind_redirect_v6>,
              ntl::wfp::filter_key<
                  ntl::wfp::layers::ale_bind_redirect_v6>,
              std::wstring, ntl::wfp::bind_redirect_selector>);
static_assert(ntl::wfp::detail::known_layer<
              ntl::wfp::layers::inbound_mac_frame_ethernet>);
static_assert(ntl::wfp::detail::known_layer<
              ntl::wfp::layers::egress_vswitch_ethernet>);
static_assert(ntl::wfp::detail::known_layer<
              ntl::wfp::layers::outbound_transport_fast>);
static_assert(ntl::wfp::detail::known_layer<
              ntl::wfp::layers::ale_endpoint_closure_v4>);
static_assert(ntl::wfp::detail::known_layer<
              ntl::wfp::layers::name_resolution_cache_v6>);
static_assert(ntl::wfp::detail::known_layer<
              ntl::wfp::layers::ipsec_v4>);
static_assert(!std::is_constructible_v<ntl::wfp::provider_ref,
                                       ntl::wfp::provider_key,
                                       std::uint64_t>);

[[maybe_unused]] void compile_policy_graph() {
  const auto application =
      ntl::wfp::application_id::current_process();
  ntl::wfp::dynamic_session session(L"ntl::wfp compile contract");
  session.install([&](ntl::wfp::policy_transaction &transaction) {
    const auto provider = transaction.add_provider(
        {provider_key, L"provider", L"compile-only provider"});
    const auto sublayer = transaction.add_sublayer(
        provider, {sublayer_key, L"sublayer", L"compile-only sublayer",
                   0x100});

    const auto connect_callout = transaction.add_callout<connect_layer>(
        provider, {connect_callout_key, L"connect", L"ALE decision"});
    ntl::wfp::filter_builder<connect_layer> connect_filter(
        connect_filter_key, L"connect filter");
    connect_filter.protocol_equal(6).remote_port_equal(443);
    transaction.add_filter(sublayer, connect_callout, connect_filter);

    const auto redirect_callout =
        transaction.add_callout<redirect_layer>(
            provider,
            {redirect_callout_key, L"redirect",
             L"local TCP proxy redirect"});
    ntl::wfp::connect_redirect_filter_builder<redirect_layer>
        redirect_filter(
            redirect_filter_key, L"redirect filter",
            ntl::wfp::local_proxy_target{1234, 8080});
    redirect_filter.application_equal(application)
        .protocol_equal(6)
        .remote_address_v4_equal(0x7f000001u)
        .remote_port_equal(443);
    transaction.add_connect_redirect_filter(
        sublayer, redirect_callout, redirect_filter);

    const auto bind_redirect_callout =
        transaction.add_callout<bind_redirect_layer>(
            provider,
            {bind_redirect_callout_key, L"bind redirect",
             L"typed local address and port redirect"});
    ntl::wfp::bind_redirect_filter_builder<bind_redirect_layer>
        bind_redirect_filter(
            bind_redirect_filter_key, L"bind redirect filter",
            ntl::wfp::bind_redirect_selector{1});
    bind_redirect_filter.application_equal(application)
        .protocol_equal(17)
        .local_port_equal(5300);
    transaction.add_bind_redirect_filter(
        sublayer, bind_redirect_callout, bind_redirect_filter);

    const auto flow_callout = transaction.add_callout<flow_layer>(
        provider, {flow_callout_key, L"flow", L"flow association"});
    ntl::wfp::inspection_filter_builder<flow_layer> flow_filter(
        flow_filter_key, L"flow filter");
    flow_filter.remote_port_equal(443);
    flow_filter.protocol_equal(6).direction_equal(FWP_DIRECTION_OUTBOUND);
    transaction.add_inspection_filter(sublayer, flow_callout, flow_filter);

    const auto stream_callout = transaction.add_callout<stream_layer>(
        provider, {stream_callout_key, L"stream", L"stream editor"});
    ntl::wfp::stream_filter_builder<stream_layer> stream_filter(
        stream_filter_key, L"stream filter");
    stream_filter.remote_port_equal(443);
    transaction.add_stream_filter(sublayer, stream_callout, stream_filter);

    ntl::wfp::stream_control_filter_builder<stream_layer>
        stream_control_filter(stream_control_filter_key,
                              L"stream control filter");
    stream_control_filter.remote_port_equal(8443);
    transaction.add_stream_control_filter(
        sublayer, stream_callout, stream_control_filter);

    const auto datagram_callout =
        transaction.add_callout<datagram_layer>(
            provider, {datagram_callout_key, L"datagram",
                       L"terminating packet callout"});
    ntl::wfp::packet_filter_builder<datagram_layer> datagram_filter(
        datagram_filter_key, L"datagram filter");
    datagram_filter.protocol_equal(17)
        .direction_equal(FWP_DIRECTION_OUTBOUND)
        .remote_port_equal(53);
    transaction.add_packet_filter(
        sublayer, datagram_callout, datagram_filter);
  });
}

} // namespace

int main() { return 0; }
