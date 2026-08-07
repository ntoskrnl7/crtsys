#include <array>
#include <cstdint>
#include <type_traits>
#include <utility>

#include <ntl/wfp/all>

namespace {

using connect_layer = ntl::wfp::layers::ale_auth_connect_v4;
using redirect_layer = ntl::wfp::layers::ale_connect_redirect_v4;
using redirect_layer_v6 = ntl::wfp::layers::ale_connect_redirect_v6;
using bind_redirect_layer = ntl::wfp::layers::ale_bind_redirect_v4;
using flow_layer = ntl::wfp::layers::ale_flow_established_v4;
using stream_layer = ntl::wfp::layers::stream_v4;
using datagram_layer = ntl::wfp::layers::datagram_data_v4;
using datagram_layer_v6 = ntl::wfp::layers::datagram_data_v6;
using outbound_ip_layer = ntl::wfp::layers::outbound_ip_packet_v4;
using mac_layer = ntl::wfp::layers::outbound_mac_frame_ethernet;
using vswitch_layer =
    ntl::wfp::layers::egress_vswitch_ethernet;
using name_cache_layer = ntl::wfp::layers::name_resolution_cache_v4;
constexpr auto fail_closed =
    ntl::wfp::callout_unavailable::block;

constexpr GUID provider_guid = {
    0x73aed1a0,
    0x5d33,
    0x429d,
    {0xa7, 0x7e, 0x4c, 0x93, 0xe6, 0xf0, 0xaa, 0x01}};
constexpr GUID sublayer_guid = {
    0x73aed1a1,
    0x5d33,
    0x429d,
    {0xa7, 0x7e, 0x4c, 0x93, 0xe6, 0xf0, 0xaa, 0x01}};
constexpr GUID connect_callout_guid = {
    0x73aed1a2,
    0x5d33,
    0x429d,
    {0xa7, 0x7e, 0x4c, 0x93, 0xe6, 0xf0, 0xaa, 0x01}};
constexpr GUID connect_filter_guid = {
    0x73aed1a3,
    0x5d33,
    0x429d,
    {0xa7, 0x7e, 0x4c, 0x93, 0xe6, 0xf0, 0xaa, 0x01}};
constexpr GUID flow_callout_guid = {
    0x73aed1a4,
    0x5d33,
    0x429d,
    {0xa7, 0x7e, 0x4c, 0x93, 0xe6, 0xf0, 0xaa, 0x01}};
constexpr GUID flow_filter_guid = {
    0x73aed1a5,
    0x5d33,
    0x429d,
    {0xa7, 0x7e, 0x4c, 0x93, 0xe6, 0xf0, 0xaa, 0x01}};
constexpr GUID stream_callout_guid = {
    0x73aed1a6,
    0x5d33,
    0x429d,
    {0xa7, 0x7e, 0x4c, 0x93, 0xe6, 0xf0, 0xaa, 0x01}};
constexpr GUID stream_filter_guid = {
    0x73aed1a7,
    0x5d33,
    0x429d,
    {0xa7, 0x7e, 0x4c, 0x93, 0xe6, 0xf0, 0xaa, 0x01}};
constexpr GUID datagram_callout_guid = {
    0x73aed1a9,
    0x5d33,
    0x429d,
    {0xa7, 0x7e, 0x4c, 0x93, 0xe6, 0xf0, 0xaa, 0x01}};
constexpr GUID datagram_filter_guid = {
    0x73aed1aa,
    0x5d33,
    0x429d,
    {0xa7, 0x7e, 0x4c, 0x93, 0xe6, 0xf0, 0xaa, 0x01}};
constexpr GUID redirect_callout_guid = {
    0x73aed1ab,
    0x5d33,
    0x429d,
    {0xa7, 0x7e, 0x4c, 0x93, 0xe6, 0xf0, 0xaa, 0x01}};
constexpr GUID redirect_filter_guid = {
    0x73aed1ac,
    0x5d33,
    0x429d,
    {0xa7, 0x7e, 0x4c, 0x93, 0xe6, 0xf0, 0xaa, 0x01}};
constexpr GUID bind_redirect_callout_guid = {
    0x73aed1ad,
    0x5d33,
    0x429d,
    {0xa7, 0x7e, 0x4c, 0x93, 0xe6, 0xf0, 0xaa, 0x01}};
constexpr GUID bind_redirect_filter_guid = {
    0x73aed1ae,
    0x5d33,
    0x429d,
    {0xa7, 0x7e, 0x4c, 0x93, 0xe6, 0xf0, 0xaa, 0x01}};
constexpr GUID enforcement_filter_guid = {
    0x73aed1b7,
    0x5d33,
    0x429d,
    {0xa7, 0x7e, 0x4c, 0x93, 0xe6, 0xf0, 0xaa, 0x01}};

constexpr ntl::wfp::provider_key provider_key(provider_guid);
constexpr ntl::wfp::sublayer_key sublayer_key(sublayer_guid);
constexpr ntl::wfp::terminating_callout_key<connect_layer>
    connect_callout_key(connect_callout_guid);
constexpr ntl::wfp::filter_key<connect_layer>
    connect_filter_key(connect_filter_guid);
constexpr ntl::wfp::inspection_callout_key<flow_layer> flow_callout_key(flow_callout_guid);
constexpr ntl::wfp::filter_key<flow_layer> flow_filter_key(flow_filter_guid);
constexpr ntl::wfp::stream_callout_key<stream_layer>
    stream_callout_key(stream_callout_guid);
constexpr ntl::wfp::filter_key<stream_layer>
    stream_filter_key(stream_filter_guid);
constexpr ntl::wfp::terminating_callout_key<datagram_layer>
    datagram_callout_key(datagram_callout_guid);
constexpr ntl::wfp::filter_key<datagram_layer>
    datagram_filter_key(datagram_filter_guid);

template <class Ref>
concept accepts_flow_inspection_filter =
    requires(ntl::wfp::policy_transaction &transaction,
             const ntl::wfp::sublayer_ref &sublayer,
             const Ref &callout,
             ntl::wfp::inspection_filter_builder<flow_layer> &filter) {
      transaction.add_inspection_filter(sublayer, callout, filter);
    };

template <class Ref>
concept accepts_datagram_packet_filter =
    requires(ntl::wfp::policy_transaction &transaction,
             const ntl::wfp::sublayer_ref &sublayer,
             const Ref &callout,
             ntl::wfp::packet_filter_builder<datagram_layer> &filter) {
      transaction.add_packet_filter(sublayer, callout, filter);
    };

template <class Ref>
concept accepts_flow_arbitration_filter =
    requires(ntl::wfp::policy_transaction &transaction,
             const ntl::wfp::sublayer_ref &sublayer,
             const Ref &callout,
             ntl::wfp::arbitration_filter_builder<flow_layer> &filter) {
      transaction.add_arbitration_filter(sublayer, callout, filter);
    };

static_assert(accepts_flow_inspection_filter<
              ntl::wfp::inspection_callout_ref<flow_layer>>);
static_assert(!accepts_flow_inspection_filter<
              ntl::wfp::terminating_callout_ref<flow_layer>>);
static_assert(accepts_datagram_packet_filter<
              ntl::wfp::terminating_callout_ref<datagram_layer>>);
static_assert(!accepts_datagram_packet_filter<
              ntl::wfp::inspection_callout_ref<datagram_layer>>);
static_assert(accepts_flow_arbitration_filter<
              ntl::wfp::arbitrating_callout_ref<flow_layer>>);
static_assert(!accepts_flow_arbitration_filter<
              ntl::wfp::inspection_callout_ref<flow_layer>>);
constexpr ntl::wfp::terminating_callout_key<redirect_layer>
    redirect_callout_key(redirect_callout_guid);
constexpr ntl::wfp::filter_key<redirect_layer>
    redirect_filter_key(redirect_filter_guid);
constexpr ntl::wfp::terminating_callout_key<bind_redirect_layer>
    bind_redirect_callout_key(bind_redirect_callout_guid);
constexpr ntl::wfp::filter_key<bind_redirect_layer>
    bind_redirect_filter_key(bind_redirect_filter_guid);
constexpr ntl::wfp::filter_key<connect_layer>
    enforcement_filter_key(enforcement_filter_guid);

static_assert(!std::is_convertible_v<ntl::wfp::provider_key, GUID>);
static_assert(!std::is_constructible_v<ntl::wfp::filter_key<connect_layer>,
                                       ntl::wfp::filter_key<flow_layer>>);
static_assert(!std::is_copy_constructible_v<ntl::wfp::policy_session>);
static_assert(!std::is_copy_constructible_v<ntl::wfp::redirected_connection>);
static_assert(ntl::wfp::local_proxy_target::from_filter_context(
                  ntl::wfp::local_proxy_target{1234, 8080}.filter_context())
                  .process_id == 1234);
static_assert(ntl::wfp::local_proxy_target::from_filter_context(
                  ntl::wfp::local_proxy_target{1234, 8080}.filter_context())
                  .port == 8080);
static_assert(ntl::wfp::local_proxy_target::from_filter_context(
                  ntl::wfp::local_proxy_target{
                      1234, 8080, ntl::wfp::original_destination_context::omit}
                      .filter_context())
                  .process_id == 1234);
static_assert(ntl::wfp::local_proxy_target::from_filter_context(
                  ntl::wfp::local_proxy_target{
                      1234, 8080, ntl::wfp::original_destination_context::omit}
                      .filter_context())
                  .context == ntl::wfp::original_destination_context::omit);
static_assert(std::is_constructible_v<
              ntl::wfp::connect_redirect_filter_builder<redirect_layer_v6>,
              ntl::wfp::filter_key<redirect_layer_v6>, std::wstring,
              ntl::wfp::local_proxy_target,
              ntl::wfp::callout_unavailable>);
static_assert(std::is_constructible_v<
              ntl::wfp::bind_redirect_filter_builder<
                  ntl::wfp::layers::ale_bind_redirect_v6>,
              ntl::wfp::filter_key<ntl::wfp::layers::ale_bind_redirect_v6>,
              std::wstring, ntl::wfp::bind_redirect_selector,
              ntl::wfp::callout_unavailable>);
static_assert(ntl::wfp::detail::known_layer<
              ntl::wfp::layers::inbound_mac_frame_ethernet>);
static_assert(
    ntl::wfp::detail::known_layer<ntl::wfp::layers::egress_vswitch_ethernet>);
static_assert(
    ntl::wfp::detail::known_layer<ntl::wfp::layers::outbound_transport_fast>);
static_assert(
    ntl::wfp::detail::internal_only_layer<
        ntl::wfp::layers::outbound_transport_fast>);
static_assert(
    !ntl::wfp::detail::filterable_layer<
        ntl::wfp::layers::outbound_transport_fast>);
static_assert(
    ntl::wfp::detail::known_layer<ntl::wfp::layers::ale_endpoint_closure_v4>);
static_assert(
    ntl::wfp::detail::known_layer<ntl::wfp::layers::name_resolution_cache_v6>);
static_assert(ntl::wfp::detail::known_layer<ntl::wfp::layers::ipsec_v4>);
static_assert(
    ntl::wfp::detail::management_only_layer<ntl::wfp::layers::ipsec_v4>);
static_assert(
    ntl::wfp::detail::filterable_layer<ntl::wfp::layers::ipsec_v4>);
static_assert(
    !ntl::wfp::detail::callout_layer<ntl::wfp::layers::ipsec_v4>);
static_assert(!std::is_constructible_v<ntl::wfp::provider_ref,
                                       ntl::wfp::provider_key, std::uint64_t>);

template <class Builder, class = void>
struct has_protocol_condition : std::false_type {};
template <class Builder>
struct has_protocol_condition<
    Builder, std::void_t<decltype(std::declval<Builder &>().protocol_equal(6))>>
    : std::true_type {};

template <class Builder, class = void>
struct has_direction_condition : std::false_type {};
template <class Builder>
struct has_direction_condition<
    Builder, std::void_t<decltype(std::declval<Builder &>().direction_equal(
                 FWP_DIRECTION_OUTBOUND))>> : std::true_type {};

template <class Builder, class = void>
struct has_application_condition : std::false_type {};
template <class Builder>
struct has_application_condition<
    Builder, std::void_t<decltype(std::declval<Builder &>().application_equal(
                 std::declval<const ntl::wfp::application_id &>()))>>
    : std::true_type {};

template <class Builder, class = void>
struct has_interface_condition : std::false_type {};
template <class Builder>
struct has_interface_condition<
    Builder,
    std::void_t<decltype(std::declval<Builder &>().interface_index_equal(1))>>
    : std::true_type {};

template <class Builder, class = void>
struct has_remote_port_condition : std::false_type {};
template <class Builder>
struct has_remote_port_condition<
    Builder,
    std::void_t<decltype(std::declval<Builder &>().remote_port_equal(443))>>
    : std::true_type {};

template <class Builder, class = void>
struct has_local_address_condition : std::false_type {};
template <class Builder>
struct has_local_address_condition<
    Builder, std::void_t<decltype(std::declval<Builder &>().local_address_equal(
                 ntl::wfp::ipv4_address::from_octets(127, 0, 0, 1)))>>
    : std::true_type {};

template <class Builder, class = void>
struct has_remote_address_condition : std::false_type {};
template <class Builder>
struct has_remote_address_condition<
    Builder,
    std::void_t<decltype(std::declval<Builder &>().remote_address_equal(
        ntl::wfp::ipv4_address::from_octets(127, 0, 0, 1)))>> : std::true_type {
};

template <class Builder, class = void>
struct has_package_condition : std::false_type {};
template <class Builder>
struct has_package_condition<
    Builder, std::void_t<decltype(std::declval<Builder &>().package_equal(
                 std::declval<const ntl::wfp::package_identity &>()))>>
    : std::true_type {};

template <class Builder, class = void>
struct has_flags_condition : std::false_type {};
template <class Builder>
struct has_flags_condition<
    Builder, std::void_t<decltype(std::declval<Builder &>().loopback())>>
    : std::true_type {};

template <class Builder, class = void>
struct has_compartment_condition : std::false_type {};
template <class Builder>
struct has_compartment_condition<
    Builder,
    std::void_t<decltype(
        std::declval<Builder &>().compartment_equal(1))>>
    : std::true_type {};

template <class Builder, class = void>
struct has_profile_condition : std::false_type {};
template <class Builder>
struct has_profile_condition<
    Builder,
    std::void_t<decltype(
        std::declval<Builder &>().current_profile_equal(
            ntl::wfp::network_profile::private_network))>>
    : std::true_type {};

template <class Builder, class = void>
struct has_reauthorization_condition : std::false_type {};
template <class Builder>
struct has_reauthorization_condition<
    Builder,
    std::void_t<decltype(
        std::declval<Builder &>().reauthorization())>>
    : std::true_type {};

template <class Builder, class = void>
struct has_interface_type_condition : std::false_type {};
template <class Builder>
struct has_interface_type_condition<
    Builder,
    std::void_t<decltype(std::declval<Builder &>().interface_type_equal(6))>>
    : std::true_type {};

template <class Builder, class = void>
struct has_icmp_condition : std::false_type {};
template <class Builder>
struct has_icmp_condition<
    Builder, std::void_t<decltype(std::declval<Builder &>().icmp_equal(8, 0))>>
    : std::true_type {};

using connect_builder = ntl::wfp::filter_builder<connect_layer>;
using stream_builder = ntl::wfp::stream_filter_builder<stream_layer>;
using datagram_builder = ntl::wfp::packet_filter_builder<datagram_layer>;
template <class Layer>
concept has_public_packet_builder = requires {
  typename ntl::wfp::packet_filter_builder<Layer>;
};
static_assert(!has_public_packet_builder<outbound_ip_layer>);
using outbound_ip_builder =
    ntl::wfp::advanced::packet_filter_builder<outbound_ip_layer>;
using udp_proxy_reply_builder =
    ntl::wfp::local_udp_proxy_reply_filter_builder<outbound_ip_layer>;
using mac_builder = ntl::wfp::packet_filter_builder<mac_layer>;
using vswitch_builder =
    ntl::wfp::packet_filter_builder<vswitch_layer>;
using name_cache_builder =
    ntl::wfp::inspection_filter_builder<name_cache_layer>;
using bind_redirect_builder =
    ntl::wfp::bind_redirect_filter_builder<bind_redirect_layer>;

static_assert(has_protocol_condition<connect_builder>::value);
static_assert(has_application_condition<connect_builder>::value);
static_assert(!has_direction_condition<connect_builder>::value);
static_assert(has_interface_condition<connect_builder>::value);
static_assert(!has_protocol_condition<stream_builder>::value);
static_assert(has_direction_condition<stream_builder>::value);
static_assert(has_remote_port_condition<stream_builder>::value);
static_assert(!has_icmp_condition<stream_builder>::value);
static_assert(has_direction_condition<datagram_builder>::value);
static_assert(has_interface_condition<datagram_builder>::value);
static_assert(has_icmp_condition<datagram_builder>::value);
static_assert(!has_protocol_condition<outbound_ip_builder>::value);
static_assert(!has_remote_port_condition<outbound_ip_builder>::value);
static_assert(has_local_address_condition<outbound_ip_builder>::value);
static_assert(has_interface_condition<outbound_ip_builder>::value);
static_assert(std::is_constructible_v<
              udp_proxy_reply_builder,
              ntl::wfp::filter_key<outbound_ip_layer>, std::wstring,
              std::uint16_t>);
static_assert(!has_protocol_condition<udp_proxy_reply_builder>::value);
static_assert(!has_remote_port_condition<udp_proxy_reply_builder>::value);
static_assert(!has_local_address_condition<udp_proxy_reply_builder>::value);
static_assert(!has_remote_port_condition<mac_builder>::value);
static_assert(has_interface_condition<mac_builder>::value);
static_assert(!has_interface_type_condition<mac_builder>::value);
static_assert(!has_remote_port_condition<bind_redirect_builder>::value);
static_assert(!has_remote_address_condition<bind_redirect_builder>::value);
static_assert(!has_remote_port_condition<name_cache_builder>::value);
static_assert(!has_protocol_condition<name_cache_builder>::value);
static_assert(!has_local_address_condition<name_cache_builder>::value);
static_assert(has_remote_address_condition<name_cache_builder>::value);
static_assert(!has_package_condition<name_cache_builder>::value);
static_assert(!has_flags_condition<name_cache_builder>::value);
static_assert(has_compartment_condition<connect_builder>::value);
static_assert(has_compartment_condition<stream_builder>::value);
static_assert(has_compartment_condition<mac_builder>::value);
static_assert(has_compartment_condition<vswitch_builder>::value);
static_assert(!has_compartment_condition<name_cache_builder>::value);
static_assert(has_profile_condition<connect_builder>::value);
static_assert(!has_profile_condition<stream_builder>::value);
static_assert(has_reauthorization_condition<connect_builder>::value);
static_assert(
    has_reauthorization_condition<bind_redirect_builder>::value);
static_assert(
    !has_reauthorization_condition<datagram_builder>::value);

template <class Layer>
struct layer_schema_probe
    : ntl::wfp::detail::typed_condition_builder<
          layer_schema_probe<Layer>, Layer> {};

template <class Layer>
consteval bool verify_layer_schema_contract() {
  using probe = layer_schema_probe<Layer>;
  using namespace ntl::wfp;
  using namespace ntl::wfp::detail;
#define NTL_CHECK(tag, expression)                                            \
  static_assert((requires(probe &value) { expression; }) ==                   \
                layer_supports_condition<Layer, tag>)
  NTL_CHECK(application_condition,
            value.application_equal(
                std::declval<const application_id &>()));
  NTL_CHECK(user_condition,
            value.user_equal(std::declval<const user_identity &>()));
  NTL_CHECK(package_condition,
            value.package_equal(std::declval<const package_identity &>()));
  NTL_CHECK(protocol_condition, value.protocol_equal(6));
  NTL_CHECK(local_port_condition, value.local_port_equal(443));
  NTL_CHECK(remote_port_condition, value.remote_port_equal(443));
  static_assert(
      ((requires(probe &value) {
          value.local_address_equal(
              ipv4_address::from_octets(127, 0, 0, 1));
        }) ||
       (requires(probe &value) {
          value.local_address_equal(ipv6_address(
              std::array<std::uint8_t, 16>{}));
        })) == layer_supports_condition<Layer, local_address_condition>);
  static_assert(
      ((requires(probe &value) {
          value.remote_address_equal(
              ipv4_address::from_octets(127, 0, 0, 1));
        }) ||
       (requires(probe &value) {
          value.remote_address_equal(ipv6_address(
              std::array<std::uint8_t, 16>{}));
        })) == layer_supports_condition<Layer, remote_address_condition>);
  NTL_CHECK(direction_condition,
            value.direction_equal(FWP_DIRECTION_OUTBOUND));
  NTL_CHECK(interface_condition, value.interface_index_equal(1));
  NTL_CHECK(subinterface_condition, value.subinterface_index_equal(1));
  NTL_CHECK(interface_type_condition, value.interface_type_equal(6));
  NTL_CHECK(compartment_condition, value.compartment_equal(1));
  NTL_CHECK(current_profile_condition,
            value.current_profile_equal(network_profile::private_network));
  NTL_CHECK(original_profile_condition,
            value.original_profile_equal(network_profile::private_network));
  NTL_CHECK(loopback_condition, value.loopback());
  NTL_CHECK(reauthorization_condition, value.reauthorization());
  NTL_CHECK(mac_frame_condition,
            value.local_mac_equal(mac_address(
                std::array<std::uint8_t, 6>{})));
  NTL_CHECK(vswitch_condition,
            value.switch_equal(std::declval<const virtual_switch_id &>()));
#undef NTL_CHECK
  return true;
}

#define NTL_VERIFY_LAYER(layer)                                               \
  static_assert(verify_layer_schema_contract<ntl::wfp::layers::layer>())
NTL_VERIFY_LAYER(ale_connect_redirect_v4);
NTL_VERIFY_LAYER(ale_connect_redirect_v6);
NTL_VERIFY_LAYER(ale_bind_redirect_v4);
NTL_VERIFY_LAYER(ale_bind_redirect_v6);
NTL_VERIFY_LAYER(ale_auth_connect_v4);
NTL_VERIFY_LAYER(ale_auth_connect_v6);
NTL_VERIFY_LAYER(ale_auth_recv_accept_v4);
NTL_VERIFY_LAYER(ale_auth_recv_accept_v6);
NTL_VERIFY_LAYER(ale_flow_established_v4);
NTL_VERIFY_LAYER(ale_flow_established_v6);
NTL_VERIFY_LAYER(stream_v4);
NTL_VERIFY_LAYER(stream_v6);
NTL_VERIFY_LAYER(datagram_data_v4);
NTL_VERIFY_LAYER(datagram_data_v6);
NTL_VERIFY_LAYER(inbound_transport_v4);
NTL_VERIFY_LAYER(inbound_transport_v6);
NTL_VERIFY_LAYER(outbound_transport_v4);
NTL_VERIFY_LAYER(outbound_transport_v6);
NTL_VERIFY_LAYER(outbound_ip_packet_v4);
NTL_VERIFY_LAYER(outbound_ip_packet_v6);
NTL_VERIFY_LAYER(ale_endpoint_closure_v4);
NTL_VERIFY_LAYER(ale_endpoint_closure_v6);
NTL_VERIFY_LAYER(name_resolution_cache_v4);
NTL_VERIFY_LAYER(name_resolution_cache_v6);
NTL_VERIFY_LAYER(ipsec_v4);
NTL_VERIFY_LAYER(ipsec_v6);
NTL_VERIFY_LAYER(inbound_mac_frame_ethernet);
NTL_VERIFY_LAYER(outbound_mac_frame_ethernet);
NTL_VERIFY_LAYER(ingress_vswitch_ethernet);
NTL_VERIFY_LAYER(egress_vswitch_ethernet);
NTL_VERIFY_LAYER(inbound_transport_fast);
NTL_VERIFY_LAYER(outbound_transport_fast);
#undef NTL_VERIFY_LAYER

[[maybe_unused]] void compile_policy_graph() {
  const auto application = ntl::wfp::application_id::current_process();
  auto session =
      ntl::wfp::policy_session::ephemeral(L"ntl::wfp compile contract");
  session.install([&](ntl::wfp::policy_transaction &transaction) {
    const auto provider = transaction.add_provider(
        {provider_key, L"provider", L"compile-only provider"});
    const auto sublayer = transaction.add_sublayer(
        provider, {sublayer_key, L"sublayer", L"compile-only sublayer", 0x100});

    const auto connect_callout = transaction.add_callout<connect_layer>(
        provider, {connect_callout_key, L"connect", L"ALE decision"});
    ntl::wfp::filter_builder<connect_layer> connect_filter(
        connect_filter_key, L"connect filter", fail_closed);
    connect_filter.protocol_equal(6).remote_port_equal(443);
    transaction.add_filter(sublayer, connect_callout, connect_filter);

    ntl::wfp::enforcement_filter_builder<connect_layer>
        static_enforcement(
            enforcement_filter_key, L"typed hard block",
            ntl::wfp::enforcement_action::block);
    static_enforcement.protocol_equal(17).remote_port_equal(9);
    transaction.add_enforcement_filter(
        sublayer, static_enforcement);

    const auto redirect_callout = transaction.add_callout<redirect_layer>(
        provider,
        {redirect_callout_key, L"redirect", L"local TCP proxy redirect"});
    ntl::wfp::connect_redirect_filter_builder<redirect_layer> redirect_filter(
        redirect_filter_key, L"redirect filter",
        ntl::wfp::local_proxy_target{1234, 8080},
        fail_closed);
    redirect_filter.application_equal(application)
        .protocol_equal(6)
        .remote_address_equal(ntl::wfp::ipv4_address::from_octets(127, 0, 0, 1))
        .remote_port_equal(443);
    transaction.add_connect_redirect_filter(sublayer, redirect_callout,
                                            redirect_filter);

    const auto bind_redirect_callout =
        transaction.add_callout<bind_redirect_layer>(
            provider, {bind_redirect_callout_key, L"bind redirect",
                       L"typed local address and port redirect"});
    ntl::wfp::bind_redirect_filter_builder<bind_redirect_layer>
        bind_redirect_filter(bind_redirect_filter_key, L"bind redirect filter",
                             ntl::wfp::bind_redirect_selector{1},
                             fail_closed);
    bind_redirect_filter.application_equal(application)
        .protocol_equal(17)
        .local_port_equal(5300);
    transaction.add_bind_redirect_filter(sublayer, bind_redirect_callout,
                                         bind_redirect_filter);

    const auto flow_callout = transaction.add_callout<flow_layer>(
        provider, {flow_callout_key, L"flow", L"flow association"});
    ntl::wfp::inspection_filter_builder<flow_layer> flow_filter(flow_filter_key,
                                                                L"flow filter");
    flow_filter.remote_port_equal(443);
    flow_filter.protocol_equal(6).direction_equal(FWP_DIRECTION_OUTBOUND);
    transaction.add_inspection_filter(sublayer, flow_callout, flow_filter);

    const auto stream_callout = transaction.add_callout<stream_layer>(
        provider, {stream_callout_key, L"stream", L"stream editor"});
    ntl::wfp::stream_filter_builder<stream_layer> stream_filter(
        stream_filter_key, L"stream filter", fail_closed);
    stream_filter.remote_port_equal(443);
    transaction.add_stream_filter(sublayer, stream_callout, stream_filter);

    const auto datagram_callout = transaction.add_callout<datagram_layer>(
        provider,
        {datagram_callout_key, L"datagram", L"terminating packet callout"});
    ntl::wfp::packet_filter_builder<datagram_layer> datagram_filter(
        datagram_filter_key, L"datagram filter", fail_closed);
    datagram_filter.protocol_equal(17)
        .direction_equal(FWP_DIRECTION_OUTBOUND)
        .remote_port_equal(53);
    transaction.add_packet_filter(sublayer, datagram_callout, datagram_filter);
  });
}

bool test_typed_condition_values() {
  ntl::wfp::packet_filter_builder<datagram_layer> ipv4(
      datagram_filter_key, L"IPv4 typed conditions", fail_closed);
  ipv4.protocol_equal(17)
      .remote_address_prefix(ntl::wfp::ipv4_network(
          ntl::wfp::ipv4_address::from_octets(10, 1, 2, 3), 8))
      .interface_index_equal(1)
      .subinterface_index_equal(0)
      .loopback(false);

  ntl::wfp::packet_filter_builder<datagram_layer> icmp(
      datagram_filter_key, L"Atomic ICMP conditions",
      fail_closed);
  icmp.icmp_equal(8, 0);

  constexpr GUID v6_filter_guid = {
      0x73aed1af,
      0x5d33,
      0x429d,
      {0xa7, 0x7e, 0x4c, 0x93, 0xe6, 0xf0, 0xaa, 0x01}};
  ntl::wfp::packet_filter_builder<datagram_layer_v6> ipv6(
      ntl::wfp::filter_key<datagram_layer_v6>(v6_filter_guid),
      L"IPv6 typed conditions", fail_closed);
  ipv6
      .local_address_equal(ntl::wfp::ipv6_address(std::array<std::uint8_t, 16>{
          0x20, 0x01, 0x0d, 0xb8, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1}))
      .remote_address_prefix(ntl::wfp::ipv6_network(
          ntl::wfp::ipv6_address(
              std::array<std::uint8_t, 16>{0x20, 0x01, 0x0d, 0xb8, 0xaa, 0xbb,
                                           0, 0, 0, 0, 0, 0, 0, 0, 0, 0}),
          32));

  constexpr GUID mac_filter_guid = {
      0x73aed1b0,
      0x5d33,
      0x429d,
      {0xa7, 0x7e, 0x4c, 0x93, 0xe6, 0xf0, 0xaa, 0x01}};
  ntl::wfp::packet_filter_builder<mac_layer> mac(
      ntl::wfp::filter_key<mac_layer>(mac_filter_guid),
      L"MAC typed conditions", fail_closed);
  mac.local_mac_equal(
         ntl::wfp::mac_address(std::array<std::uint8_t, 6>{0, 1, 2, 3, 4, 5}))
      .remote_mac_equal(ntl::wfp::mac_address(
          std::array<std::uint8_t, 6>{6, 7, 8, 9, 10, 11}))
      .ether_type_equal(0x0800)
      .vlan_id_equal(100)
      .interface_index_equal(1);

  const auto user = ntl::wfp::user_identity::from_string(L"S-1-5-18");
  const auto package = ntl::wfp::package_identity::from_string(L"S-1-15-2-1");
  ntl::wfp::filter_builder<connect_layer> identity(connect_filter_key,
                                                   L"identity conditions",
                                                   fail_closed);
  identity.user_equal(user).package_equal(package);

  constexpr GUID vswitch_filter_guid = {
      0x73aed1b3,
      0x5d33,
      0x429d,
      {0xa7, 0x7e, 0x4c, 0x93, 0xe6, 0xf0, 0xaa, 0x01}};
  constexpr GUID switch_guid = {
      0x73aed1b4,
      0x5d33,
      0x429d,
      {0xa7, 0x7e, 0x4c, 0x93, 0xe6, 0xf0, 0xaa, 0x01}};
  constexpr GUID interface_guid = {
      0x73aed1b5,
      0x5d33,
      0x429d,
      {0xa7, 0x7e, 0x4c, 0x93, 0xe6, 0xf0, 0xaa, 0x01}};
  constexpr GUID vm_guid = {
      0x73aed1b6,
      0x5d33,
      0x429d,
      {0xa7, 0x7e, 0x4c, 0x93, 0xe6, 0xf0, 0xaa, 0x01}};
  ntl::wfp::packet_filter_builder<vswitch_layer> vswitch(
      ntl::wfp::filter_key<vswitch_layer>(
          vswitch_filter_guid),
      L"vSwitch typed conditions", fail_closed);
  vswitch
      .switch_equal(ntl::wfp::virtual_switch_id(
          switch_guid))
      .source_interface_equal(
          ntl::wfp::virtual_switch_interface_id(
              interface_guid))
      .source_interface_type_equal(
          ntl::wfp::virtual_switch_interface_type::synthetic)
      .source_vm_equal(
          ntl::wfp::virtual_machine_id(vm_guid))
      .network_type_equal(
          ntl::wfp::virtual_switch_network::internal)
      .tenant_network_equal(42)
      .compartment_equal(1);

  ntl::wfp::filter_builder<connect_layer> comparisons(
      connect_filter_key, L"safe comparison conditions",
      fail_closed);
  comparisons.protocol_not_equal(17)
      .local_port_range(1024, 65535)
      .remote_port_not_equal(80)
      .remote_address_not_equal(
          ntl::wfp::ipv4_address::from_octets(
              192, 0, 2, 1))
      .compartment_range(1, 32)
      .current_profile_not_equal(
          ntl::wfp::network_profile::public_network)
      .reauthorization(false);

  return ipv4.condition_count() == 5 && icmp.condition_count() == 3 &&
         ipv6.condition_count() == 2 && mac.condition_count() == 5 &&
         identity.condition_count() == 2 &&
         comparisons.condition_count() == 7 &&
         vswitch.condition_count() == 7;
}

bool test_network_event_snapshot() {
  std::array<std::uint8_t, 4> application{1, 2, 3, 4};
  FWPM_NET_EVENT_CLASSIFY_DROP1 drop{};
  drop.filterId = 42;
  drop.layerId = 7;
  drop.msFwpDirection = FWP_DIRECTION_OUTBOUND;

  FWPM_NET_EVENT1 event{};
  event.header.flags =
      FWPM_NET_EVENT_FLAG_IP_VERSION_SET | FWPM_NET_EVENT_FLAG_IP_PROTOCOL_SET |
      FWPM_NET_EVENT_FLAG_LOCAL_ADDR_SET | FWPM_NET_EVENT_FLAG_REMOTE_ADDR_SET |
      FWPM_NET_EVENT_FLAG_LOCAL_PORT_SET | FWPM_NET_EVENT_FLAG_REMOTE_PORT_SET |
      FWPM_NET_EVENT_FLAG_APP_ID_SET;
  event.header.ipVersion = FWP_IP_VERSION_V4;
  event.header.ipProtocol = 6;
  event.header.localAddrV4 = 0x7f000001u;
  event.header.remoteAddrV4 = 0x0a000001u;
  event.header.localPort = 1234;
  event.header.remotePort = 443;
  event.header.appId.size = static_cast<UINT32>(application.size());
  event.header.appId.data = application.data();
  event.type = FWPM_NET_EVENT_TYPE_CLASSIFY_DROP;
  event.classifyDrop = &drop;

  const auto snapshot = ntl::wfp::detail::snapshot_network_event(event);
  return snapshot.kind == ntl::wfp::network_event_kind::classify_drop &&
         snapshot.address_size == 4 && snapshot.local_address[0] == 127 &&
         snapshot.local_address[3] == 1 && snapshot.remote_address[0] == 10 &&
         snapshot.protocol == 6 && snapshot.local_port == 1234 &&
         snapshot.remote_port == 443 && snapshot.application_id_hash != 0 &&
         snapshot.filter_id == 42 && snapshot.layer_id == 7 &&
         snapshot.direction == FWP_DIRECTION_OUTBOUND;
}

bool test_condition_validation_stress() {
  std::uint32_t state = 0x9e3779b9u;
  const auto next = [&state]() noexcept {
    state ^= state << 13;
    state ^= state >> 17;
    state ^= state << 5;
    return state;
  };

  constexpr GUID v6_filter_guid = {
      0x73aed1b1,
      0x5d33,
      0x429d,
      {0xa7, 0x7e, 0x4c, 0x93, 0xe6, 0xf0, 0xaa, 0x01}};
  const ntl::wfp::filter_key<datagram_layer_v6> v6_key(v6_filter_guid);

  for (std::uint8_t prefix = 0; prefix <= 32; ++prefix) {
    ntl::wfp::packet_filter_builder<datagram_layer> builder(
        datagram_filter_key, L"IPv4 prefix property",
        fail_closed);
    builder.remote_address_prefix(ntl::wfp::ipv4_network(
        ntl::wfp::ipv4_address::from_host_order(next()), prefix));
    if (builder.condition_count() != 1)
      return false;
  }

  for (std::uint16_t prefix = 0; prefix <= 128; ++prefix) {
    std::array<std::uint8_t, 16> bytes{};
    for (auto &byte : bytes)
      byte = static_cast<std::uint8_t>(next());
    ntl::wfp::packet_filter_builder<datagram_layer_v6> builder(
        v6_key, L"IPv6 prefix property", fail_closed);
    builder.remote_address_prefix(ntl::wfp::ipv6_network(
        ntl::wfp::ipv6_address(bytes), static_cast<std::uint8_t>(prefix)));
    if (builder.condition_count() != 1)
      return false;
  }

  bool duplicate_rejected = false;
  try {
    ntl::wfp::packet_filter_builder<datagram_layer> duplicate(
        datagram_filter_key, L"Duplicate field rejection",
        fail_closed);
    duplicate.remote_port_equal(53).remote_port_equal(5353);
  } catch (const std::logic_error &) {
    duplicate_rejected = true;
  }

  bool invalid_v4_prefix = false;
  try {
    (void)ntl::wfp::ipv4_network(ntl::wfp::ipv4_address::from_host_order(0),
                                 33);
  } catch (const std::invalid_argument &) {
    invalid_v4_prefix = true;
  }

  bool invalid_v6_prefix = false;
  try {
    (void)ntl::wfp::ipv6_network(ntl::wfp::ipv6_address({}), 129);
  } catch (const std::invalid_argument &) {
    invalid_v6_prefix = true;
  }

  bool invalid_direction = false;
  try {
    ntl::wfp::packet_filter_builder<datagram_layer> direction(
        datagram_filter_key, L"Direction validation",
        fail_closed);
    direction.direction_equal(17);
  } catch (const std::invalid_argument &) {
    invalid_direction = true;
  }

  bool invalid_compartment = false;
  try {
    ntl::wfp::packet_filter_builder<datagram_layer> compartment(
        datagram_filter_key, L"Compartment validation",
        fail_closed);
    compartment.compartment_equal(0);
  } catch (const std::invalid_argument &) {
    invalid_compartment = true;
  }

  bool invalid_vlan = false;
  try {
    ntl::wfp::packet_filter_builder<mac_layer> vlan(
        ntl::wfp::filter_key<mac_layer>(
            {0x73aed1b2,
             0x5d33,
             0x429d,
             {0xa7, 0x7e, 0x4c, 0x93, 0xe6, 0xf0, 0xaa, 0x01}}),
        L"VLAN validation", fail_closed);
    vlan.vlan_id_equal(4096);
  } catch (const std::invalid_argument &) {
    invalid_vlan = true;
  }

  bool invalid_port_range = false;
  try {
    ntl::wfp::packet_filter_builder<datagram_layer> range(
        datagram_filter_key, L"Port range validation",
        fail_closed);
    range.remote_port_range(65535, 1024);
  } catch (const std::invalid_argument &) {
    invalid_port_range = true;
  }

  bool conflicting_vswitch_network = false;
  try {
    ntl::wfp::packet_filter_builder<vswitch_layer> network(
        ntl::wfp::filter_key<vswitch_layer>(
            {0x73aed1b7,
             0x5d33,
             0x429d,
             {0xa7, 0x7e, 0x4c, 0x93, 0xe6, 0xf0, 0xaa, 0x01}}),
        L"vSwitch network validation", fail_closed);
    network.vlan_id_equal(100)
        .tenant_network_equal(42);
  } catch (const std::logic_error &) {
    conflicting_vswitch_network = true;
  }

  return duplicate_rejected && invalid_v4_prefix && invalid_v6_prefix &&
         invalid_direction && invalid_compartment && invalid_vlan &&
         invalid_port_range && conflicting_vswitch_network;
}

} // namespace

int main() {
  return test_typed_condition_values() && test_network_event_snapshot() &&
                 test_condition_validation_stress()
             ? 0
             : 1;
}
