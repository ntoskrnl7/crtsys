#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <guiddef.h>
#include <ntl/wfp/layers>
#include <ntl/wfp/transparent_udp_proxy_contract>

#ifndef CTL_CODE
#error Include <wdm.h> in kernel mode or <winioctl.h> in user mode first.
#endif

namespace wfp_kernel_browser_https_inspection {

using redirect_layer_v4 = ntl::wfp::layers::ale_connect_redirect_v4;
using redirect_layer_v6 = ntl::wfp::layers::ale_connect_redirect_v6;
using quic_layer_v4 = ntl::wfp::layers::ale_auth_connect_v4;
using quic_layer_v6 = ntl::wfp::layers::ale_auth_connect_v6;
using quic_flow_layer_v4 = ntl::wfp::layers::ale_flow_established_v4;
using quic_flow_layer_v6 = ntl::wfp::layers::ale_flow_established_v6;
using quic_datagram_layer_v4 = ntl::wfp::layers::datagram_data_v4;
using quic_datagram_layer_v6 = ntl::wfp::layers::datagram_data_v6;
using quic_reverse_layer_v4 = ntl::wfp::layers::outbound_ip_packet_v4;
using quic_reverse_layer_v6 = ntl::wfp::layers::outbound_ip_packet_v6;

inline constexpr wchar_t service_name[] =
    L"crtsys_wfp_kernel_browser_https_inspection";
inline constexpr wchar_t device_name[] =
    L"CrtSysWfpKernelBrowserHttpsInspection";
inline constexpr wchar_t user_device_path[] =
    L"\\\\.\\CrtSysWfpKernelBrowserHttpsInspection";
inline constexpr std::uint32_t telemetry_version = 3;
inline constexpr std::size_t certificate_thumbprint_size = 20;
inline constexpr std::size_t maximum_certificate_der_size = 16 * 1024;
inline constexpr std::size_t maximum_server_name_size = 253;
inline constexpr std::size_t maximum_capture_size = 4096;
inline constexpr std::uint32_t identity_cache_capacity = 32;

enum class inspected_protocol : std::uint32_t {
  none = 0,
  http1 = 1,
  http2 = 2,
  http3 = 3,
};

enum class inspection_action : std::uint32_t {
  none = 0,
  permitted = 1,
  blocked = 2,
  failed = 3,
};

enum inspection_flags : std::uint32_t {
  request_transformed = 0x00000001,
  response_transformed = 0x00000002,
  compressed_content = 0x00000004,
  websocket_or_extended_connect = 0x00000008,
  datagram_or_webtransport = 0x00000010,
  html_content = 0x00000020,
  grpc_message = 0x00000040,
  failure_request_direction = 0x01000000,
  failure_response_direction = 0x02000000,
  failure_source_read = 0x00010000,
  failure_destination_write = 0x00020000,
  failure_source_write = 0x00040000,
  failure_protocol = 0x00080000,
  failure_tls_close_generate = 0x00100000,
  failure_tls_close_send = 0x00200000,
  failure_tls_close_receive = 0x00400000,
  failure_tls_transport_half_close = 0x00800000,
  failure_http2_relay = 0x10000000,
  failure_origin_tls_close = 0x20000000,
  failure_browser_tls_close = 0x40000000,
};

struct certificate_config {
  std::array<std::byte, certificate_thumbprint_size> sha1_thumbprint;
  std::uint32_t server_name_size;
  std::array<char, maximum_server_name_size + 1> server_name;
};

enum class origin_security_action : std::uint32_t {
  install = 1,
  remove = 2,
};

/**
 * Host-scoped outbound identity plus an exact, system-validated origin leaf.
 *
 * A managed acceptance run must remove the entry before deleting its
 * temporary certificates.  Entries never change the policy of an unrelated
 * browser origin.
 */
struct origin_security_config {
  origin_security_action action;
  std::uint32_t server_name_size;
  std::array<char, maximum_server_name_size + 1> server_name;
  std::array<std::byte, certificate_thumbprint_size>
      client_sha1_thumbprint;
  std::uint32_t origin_leaf_der_size;
  std::array<std::byte, maximum_certificate_der_size> origin_leaf_der;
};

inline std::uint64_t hash_application_id(const std::uint8_t *data,
                                         std::size_t size) noexcept {
  constexpr std::uint64_t offset = 14695981039346656037ull;
  constexpr std::uint64_t prime = 1099511628211ull;
  std::uint64_t result = offset;
  for (std::size_t index = 0; index != size; ++index) {
    result ^= data[index];
    result *= prime;
  }
  return result;
}

struct alignas(8) quic_layer_telemetry {
  std::uint64_t classify_hits;
  std::uint64_t block_decisions;
  std::uint64_t action_write_available;
  std::uint64_t action_write_missing;
  std::uint64_t initial_permit;
  std::uint64_t last_filter_id;
  std::uint64_t last_process_id;
  std::uint64_t last_application_id_hash;
  std::uint32_t last_application_id_size;
  std::uint32_t last_remote_address_v4;
  std::uint32_t last_remote_address_v6[4];
  std::uint16_t last_remote_port;
  std::uint16_t last_filter_flags;
  std::uint8_t last_protocol;
  std::uint8_t address_family;
  std::uint16_t reserved;
};

struct alignas(8) udp_translation_telemetry {
  std::uint64_t outbound_packets;
  std::uint64_t inbound_packets;
  std::uint64_t mapping_updates;
  std::uint64_t mapping_misses;
  std::uint64_t injection_failures;
  std::uint64_t quota_rejections;
  std::uint32_t last_mapping_family;
  std::uint32_t last_mapping_source_port;
  std::uint32_t last_mapping_destination_port;
  std::uint32_t last_mapping_proxy_port;
  std::uint32_t last_resolution_peer_family;
  std::uint32_t last_resolution_peer_port;
  std::uint32_t last_resolution_proxy_family;
  std::uint32_t last_resolution_proxy_port;
  std::int32_t last_resolution_status;
  std::uint32_t reserved;
};

struct alignas(8) quic_telemetry {
  std::uint32_t version;
  std::uint32_t size;
  quic_layer_telemetry ipv4;
  quic_layer_telemetry ipv6;
  udp_translation_telemetry translation;
};

inline constexpr std::uint32_t service_info_version = 5;

enum class webtransport_rejection_stage : std::uint32_t {
  none = 0,
  transport_requirements = 1,
  request_validation = 2,
  request_transform_explicit_block = 3,
  request_transform_wrong_protocol = 4,
  request_transform_wrong_upgrade_token = 5,
  request_transform_failure = 6,
  request_transform_unexpected = 7,
  staged_decision = 8,
  session_accept = 9,
};

enum webtransport_request_state_flag : std::uint32_t {
  webtransport_method_connect = 1u << 0,
  webtransport_protocol_http3 = 1u << 1,
  webtransport_upgrade_token = 1u << 2,
  webtransport_explicit_block = 1u << 3,
  webtransport_body_block_marker = 1u << 4,
};

enum class origin_failure_stage : std::uint32_t {
  none = 0,
  strict_http3 = 1,
  fallback_create = 2,
  fallback_exchange = 3,
};

enum class origin_fallback_phase : std::uint32_t {
  none = 0,
  resolve_origin = 1,
  connect_origin = 2,
  create_transport_stream = 3,
  create_tls_stream = 4,
  tls_handshake = 5,
  http1_write_request = 6,
  http1_read_response = 7,
  http2_write_opening = 8,
  http2_read_peer_settings = 9,
  http2_write_request = 10,
  http2_read_response = 11,
  tls_shutdown = 12,
  complete = 13,
};

struct service_info {
  std::uint32_t version;
  std::uint32_t size;
  std::uint32_t process_id;
  std::uint16_t tcp_port_v4;
  std::uint16_t tcp_port_v6;
  std::uint16_t http3_port;
  std::uint16_t reserved;
  std::uint32_t tcp_ready;
  std::uint32_t http3_ready;
  std::uint32_t workspace_lifetime_passed;
  std::uint32_t reserved2;
  std::uint64_t accepted;
  std::uint64_t handshaken;
  std::uint64_t origin_connected;
  std::uint64_t origin_completed;
  std::uint64_t permitted;
  std::uint64_t blocked;
  std::uint64_t transformed;
  std::uint64_t failed;
  std::uint64_t identity_requests;
  std::uint64_t identity_timeouts;
  std::uint64_t capture_dropped;
  std::uint64_t http3_accepted;
  std::uint64_t http3_permitted;
  std::uint64_t http3_blocked;
  std::uint64_t http3_failed;
  std::uint64_t http3_origin_connected;
  std::uint64_t http3_origin_completed;
  std::uint64_t http3_origin_failed;
  std::uint64_t http3_origin_peer_validated;
  std::uint64_t http3_origin_h3_negotiated;
  std::uint64_t http3_origin_peer_settings;
  std::uint64_t http3_origin_qpack_acknowledgements;
  std::uint64_t origin_fallback_attempted;
  std::uint64_t origin_fallback_succeeded;
  std::uint64_t origin_fallback_h2;
  std::uint64_t origin_fallback_http1;
  std::uint64_t origin_fallback_rejected;
  std::int32_t origin_last_status;
  std::uint32_t origin_last_failure_kind;
  origin_failure_stage origin_last_failure_stage;
  origin_fallback_phase origin_last_fallback_phase;
  std::uint64_t http3_active_connections;
  std::uint64_t http3_peak_connections;
  std::uint64_t http3_reaped_connections;
  std::uint64_t http3_worker_requests;
  std::uint64_t http3_irql_violations;
  std::uint64_t qpack_resumed;
  std::uint64_t gzip_responses;
  std::uint64_t deflate_responses;
  std::uint64_t brotli_responses;
  std::uint64_t webtransport_sessions;
  std::uint64_t webtransport_bidirectional;
  std::uint64_t webtransport_unidirectional;
  std::uint64_t webtransport_datagrams;
  std::uint64_t webtransport_capsules;
  std::uint64_t webtransport_resets;
  webtransport_rejection_stage webtransport_last_rejection_stage;
  std::int32_t webtransport_last_rejection_status;
  std::uint32_t webtransport_last_transform_action;
  std::uint32_t webtransport_last_transform_rule;
  std::uint32_t webtransport_last_transform_before_flags;
  std::uint32_t webtransport_last_transform_after_flags;
  std::uint32_t webtransport_last_transform_body_size;
  std::uint64_t http3_buffered_request_bytes;
  std::uint64_t http3_peak_buffered_request_bytes;
  std::uint64_t http3_buffer_quota_rejections;
  std::uint64_t http3_canceled_streams;
  std::uint64_t http3_pending_requests;
  std::uint64_t http3_peak_pending_requests;
  std::uint64_t http3_origin_allocation_bytes;
  std::uint64_t http3_origin_peak_allocation_bytes;
  std::uint64_t http3_origin_allocation_quota_rejections;
  std::uint64_t origin_peer_validated;
  std::uint32_t identity_count;
  std::uint32_t identity_capacity;
  std::uint32_t origin_security_ready;
  std::uint32_t http3_origin_security_ready;
  std::uint32_t active_tcp_sessions;
  std::uint32_t http3_identity_count;
  std::uint64_t http3_peer_bidirectional_started;
  std::uint64_t http3_peer_unidirectional_started;
  std::uint64_t http3_peer_receive_events;
  std::uint64_t http3_peer_receive_fin_events;
  std::uint64_t http3_peer_send_shutdown_events;
  std::uint64_t http3_request_streams_classified;
  std::uint64_t http3_request_sink_calls;
  std::uint64_t http3_request_sink_final_calls;
  std::int32_t http3_last_request_sink_status;
  std::int32_t http3_last_stream_rejection_status;
  std::uint64_t http3_proxy_request_stream_calls;
  std::uint64_t http3_proxy_request_stream_final_calls;
  std::uint64_t http3_proxy_request_inspector_retries;
  std::uint64_t http3_proxy_request_headers;
  std::uint64_t http3_proxy_request_stream_ends;
  std::uint64_t http3_proxy_blocked_request_streams;
  std::uint64_t http3_proxy_active_requests;
  std::uint64_t http3_proxy_origin_submit_calls;
  std::int32_t http3_proxy_last_stream_end_status;
  std::int32_t http3_proxy_last_origin_submit_status;
  quic_telemetry quic_gate;
};

inline bool valid_service_info_abi(const service_info &value) noexcept {
  return value.version == service_info_version &&
         value.size == static_cast<std::uint32_t>(sizeof(service_info));
}

struct inspection_record {
  std::uint64_t sequence;
  std::uint64_t session_id;
  std::uint16_t original_family;
  std::uint16_t original_port;
  std::array<std::byte, 16> original_address;
  std::uint32_t server_name_size;
  std::array<char, maximum_server_name_size + 1> server_name;
  inspected_protocol protocol;
  inspection_action action;
  std::uint32_t flags;
  std::uint32_t status;
  std::int32_t failure_status;
  std::uint32_t request_size;
  std::uint32_t response_size;
  std::array<std::byte, maximum_capture_size> request;
  std::array<std::byte, maximum_capture_size> response;
};

struct sequence_cursor {
  std::uint64_t after_sequence;
};

struct inspection_read_result {
  std::uint32_t available;
  std::uint32_t dropped;
  std::uint64_t oldest_sequence;
  std::uint64_t current_sequence;
  inspection_record record;
};

struct identity_request {
  std::uint64_t sequence;
  std::uint64_t session_id;
  std::uint32_t server_name_size;
  std::array<char, maximum_server_name_size + 1> server_name;
};

struct identity_request_read_result {
  std::uint32_t available;
  std::uint32_t dropped;
  std::uint64_t oldest_sequence;
  std::uint64_t current_sequence;
  identity_request request;
};

template <unsigned long Function, class Input, class Output,
          unsigned long Access = FILE_READ_DATA | FILE_WRITE_DATA>
struct ioctl_contract {
  static constexpr unsigned long device_type = FILE_DEVICE_UNKNOWN;
  static constexpr unsigned long function = Function;
  static constexpr unsigned long method = METHOD_BUFFERED;
  static constexpr unsigned long access = Access;
  using input_type = Input;
  using output_type = Output;
};

using configure_identity_contract =
    ioctl_contract<0x974, certificate_config, void, FILE_WRITE_DATA>;
using query_service_contract =
    ioctl_contract<0x975, void, service_info, FILE_READ_DATA>;
using read_inspection_contract =
    ioctl_contract<0x976, sequence_cursor, inspection_read_result,
                   FILE_READ_DATA>;
using read_identity_request_contract =
    ioctl_contract<0x977, sequence_cursor, identity_request_read_result,
                   FILE_READ_DATA>;
using query_telemetry_contract =
    ioctl_contract<0x978, void, quic_telemetry, FILE_READ_DATA>;
using configure_origin_security_contract =
    ioctl_contract<0x979, origin_security_config, void, FILE_WRITE_DATA>;
// Managed-acceptance fault injection: the next H3 origin-security publish
// fails before mutation so the cross-service rollback path is deterministic.
using arm_origin_security_rollback_test_contract =
    ioctl_contract<0x97a, void, void, FILE_WRITE_DATA>;

inline constexpr unsigned long configure_identity_ioctl =
    CTL_CODE(configure_identity_contract::device_type,
             configure_identity_contract::function,
             configure_identity_contract::method,
             configure_identity_contract::access);
inline constexpr unsigned long query_service_ioctl =
    CTL_CODE(query_service_contract::device_type,
             query_service_contract::function,
             query_service_contract::method,
             query_service_contract::access);
inline constexpr unsigned long read_inspection_ioctl =
    CTL_CODE(read_inspection_contract::device_type,
             read_inspection_contract::function,
             read_inspection_contract::method,
             read_inspection_contract::access);
inline constexpr unsigned long read_identity_request_ioctl =
    CTL_CODE(read_identity_request_contract::device_type,
             read_identity_request_contract::function,
             read_identity_request_contract::method,
             read_identity_request_contract::access);

inline constexpr unsigned long query_telemetry_ioctl =
    CTL_CODE(query_telemetry_contract::device_type,
             query_telemetry_contract::function,
             query_telemetry_contract::method,
             query_telemetry_contract::access);
inline constexpr unsigned long configure_origin_security_ioctl =
    CTL_CODE(configure_origin_security_contract::device_type,
             configure_origin_security_contract::function,
             configure_origin_security_contract::method,
             configure_origin_security_contract::access);
inline constexpr unsigned long arm_origin_security_rollback_test_ioctl =
    CTL_CODE(arm_origin_security_rollback_test_contract::device_type,
             arm_origin_security_rollback_test_contract::function,
             arm_origin_security_rollback_test_contract::method,
             arm_origin_security_rollback_test_contract::access);

static_assert(sizeof(quic_layer_telemetry) == 96);
static_assert(sizeof(udp_translation_telemetry) == 88);
static_assert(sizeof(quic_telemetry) == 288);

// {8D8FCD68-31BD-4A98-9029-074309E46F19}
inline constexpr GUID device_class_guid{
    0x8d8fcd68, 0x31bd, 0x4a98,
    {0x90, 0x29, 0x07, 0x43, 0x09, 0xe4, 0x6f, 0x19}};
// Unique MsQuic kernel client identity for this self-contained driver.
// {C066C521-2A3D-4512-A496-767CCBBD6C62}
inline constexpr GUID msquic_module_id{
    0xc066c521, 0x2a3d, 0x4512,
    {0xa4, 0x96, 0x76, 0x7c, 0xcb, 0xbd, 0x6c, 0x62}};
// {962FB7C1-4B4F-4E6E-ACE9-E0CFA5901E6A}
inline constexpr ntl::wfp::provider_key provider_key{
    GUID{0x962fb7c1, 0x4b4f, 0x4e6e,
         {0xac, 0xe9, 0xe0, 0xcf, 0xa5, 0x90, 0x1e, 0x6a}}};
// {B24803F9-34CA-4969-9C9A-529FCCFD2FE9}
inline constexpr ntl::wfp::sublayer_key sublayer_key{
    GUID{0xb24803f9, 0x34ca, 0x4969,
         {0x9c, 0x9a, 0x52, 0x9f, 0xcc, 0xfd, 0x2f, 0xe9}}};
// {CF885E2F-ADC1-41D3-93BF-7B244B465E01}
inline constexpr ntl::wfp::terminating_callout_key<redirect_layer_v4>
    redirect_callout_key_v4{
        GUID{0xcf885e2f, 0xadc1, 0x41d3,
             {0x93, 0xbf, 0x7b, 0x24, 0x4b, 0x46, 0x5e, 0x01}}};
// {2FF45F12-93D5-45A0-83E5-715400560B85}
inline constexpr ntl::wfp::filter_key<redirect_layer_v4>
    redirect_filter_key_v4{
        GUID{0x2ff45f12, 0x93d5, 0x45a0,
             {0x83, 0xe5, 0x71, 0x54, 0x00, 0x56, 0x0b, 0x85}}};
// {137A74D9-3E92-41BF-AC16-3148349165AF}
inline constexpr ntl::wfp::terminating_callout_key<redirect_layer_v6>
    redirect_callout_key_v6{
        GUID{0x137a74d9, 0x3e92, 0x41bf,
             {0xac, 0x16, 0x31, 0x48, 0x34, 0x91, 0x65, 0xaf}}};
// {49E3CE5B-2DA7-4540-B8DE-F5419E46B2A1}
inline constexpr ntl::wfp::filter_key<redirect_layer_v6>
    redirect_filter_key_v6{
        GUID{0x49e3ce5b, 0x2da7, 0x4540,
             {0xb8, 0xde, 0xf5, 0x41, 0x9e, 0x46, 0xb2, 0xa1}}};
// Managed acceptance: remember the client tuple before packet translation.
// {DA271DEE-F724-43EF-8452-A36DABAC0119}
inline constexpr ntl::wfp::filter_key<quic_flow_layer_v4>
    managed_http3_flow_filter_key_v4{
        GUID{0xda271dee, 0xf724, 0x43ef,
             {0x84, 0x52, 0xa3, 0x6d, 0xab, 0xac, 0x01, 0x19}}};
// {157A2A2B-63D0-41C2-A8B5-88C32B893EE0}
inline constexpr ntl::wfp::filter_key<quic_flow_layer_v6>
    managed_http3_flow_filter_key_v6{
        GUID{0x157a2a2b, 0x63d0, 0x41c2,
             {0xa8, 0xb5, 0x88, 0xc3, 0x2b, 0x89, 0x3e, 0xe0}}};
// Bidirectional UDP tuple translation callouts and packet filters.
inline constexpr ntl::wfp::arbitrating_callout_key<quic_flow_layer_v4>
    quic_flow_callout_key_v4{
        GUID{0x5a2b02f4, 0x3120, 0x49ab,
             {0x97, 0xb0, 0x51, 0x62, 0xa2, 0xd8, 0x7f, 0x10}}};
inline constexpr ntl::wfp::arbitrating_callout_key<quic_flow_layer_v6>
    quic_flow_callout_key_v6{
        GUID{0x793ca8f2, 0x5c16, 0x47ad,
             {0xb2, 0xb0, 0x73, 0x08, 0x7d, 0x45, 0x95, 0x21}}};
inline constexpr ntl::wfp::terminating_callout_key<quic_datagram_layer_v4>
    quic_datagram_callout_key_v4{
        GUID{0x6b3d1195, 0xca8b, 0x48fd,
             {0xa3, 0x49, 0x63, 0x1b, 0xcf, 0x55, 0x94, 0x82}}};
inline constexpr ntl::wfp::terminating_callout_key<quic_datagram_layer_v6>
    quic_datagram_callout_key_v6{
        GUID{0x8223a1c0, 0x6d96, 0x4288,
              {0x9a, 0xcf, 0xc3, 0x85, 0x4f, 0x3a, 0x12, 0xd7}}};
inline constexpr ntl::wfp::terminating_callout_key<quic_reverse_layer_v4>
    quic_reverse_callout_key_v4{
        GUID{0x42b9d0fa, 0x23a7, 0x46a5,
             {0xb0, 0x4a, 0x00, 0x63, 0x0f, 0x33, 0xa7, 0x6a}}};
inline constexpr ntl::wfp::terminating_callout_key<quic_reverse_layer_v6>
    quic_reverse_callout_key_v6{
        GUID{0xb86203c4, 0x5523, 0x492a,
             {0xac, 0x9c, 0x35, 0x2d, 0x26, 0x73, 0x92, 0x46}}};
inline constexpr ntl::wfp::filter_key<quic_datagram_layer_v4>
    managed_http3_outbound_filter_key_v4{
        GUID{0x976cc9bd, 0x2d30, 0x4ce6,
             {0x84, 0x26, 0xa9, 0xb3, 0x26, 0xc8, 0xe4, 0x14}}};
inline constexpr ntl::wfp::filter_key<quic_datagram_layer_v6>
    managed_http3_outbound_filter_key_v6{
        GUID{0xb1f1ea49, 0xb2f3, 0x42b6,
             {0x90, 0x84, 0x29, 0x88, 0x7c, 0xf4, 0x03, 0x61}}};
inline constexpr ntl::wfp::filter_key<quic_reverse_layer_v4>
    managed_http3_reverse_filter_key_v4{
        GUID{0xc8322890, 0x74fa, 0x48ce,
             {0x9d, 0x0a, 0xfd, 0x5a, 0x16, 0xe2, 0x64, 0xb9}}};
inline constexpr ntl::wfp::filter_key<quic_reverse_layer_v6>
    managed_http3_reverse_filter_key_v6{
        GUID{0xde96426e, 0x5551, 0x44dd,
             {0xa7, 0xec, 0x43, 0x17, 0x2d, 0x9e, 0x0f, 0x35}}};

inline constexpr ntl::wfp::transparent_udp_proxy_keys udp_proxy_keys{
    provider_key,
    sublayer_key,
    quic_flow_callout_key_v4,
    quic_flow_callout_key_v6,
    quic_datagram_callout_key_v4,
    quic_datagram_callout_key_v6,
    quic_reverse_callout_key_v4,
    quic_reverse_callout_key_v6,
    managed_http3_flow_filter_key_v4,
    managed_http3_flow_filter_key_v6,
    managed_http3_outbound_filter_key_v4,
    managed_http3_outbound_filter_key_v6,
    managed_http3_reverse_filter_key_v4,
    managed_http3_reverse_filter_key_v6};
// {0507D369-1318-413F-8BBA-2D4E0CD34F31}
inline constexpr ntl::wfp::inspection_callout_key<quic_layer_v4> callout_key_v4{
    GUID{0x0507d369, 0x1318, 0x413f,
         {0x8b, 0xba, 0x2d, 0x4e, 0x0c, 0xd3, 0x4f, 0x31}}};
// {3CBA38E3-4067-4F22-9F64-D70C4F11176C}
inline constexpr ntl::wfp::filter_key<quic_layer_v4> filter_key_v4{
    GUID{0x3cba38e3, 0x4067, 0x4f22,
         {0x9f, 0x64, 0xd7, 0x0c, 0x4f, 0x11, 0x17, 0x6c}}};
// {CE499F95-D0D6-494F-B575-8D5A1F002CC3}
inline constexpr ntl::wfp::inspection_callout_key<quic_layer_v6> callout_key_v6{
    GUID{0xce499f95, 0xd0d6, 0x494f,
         {0xb5, 0x75, 0x8d, 0x5a, 0x1f, 0x00, 0x2c, 0xc3}}};
// {00B6C17F-F7B6-4606-AA22-CEC149F802E1}
inline constexpr ntl::wfp::filter_key<quic_layer_v6> filter_key_v6{
    GUID{0x00b6c17f, 0xf7b6, 0x4606,
         {0xaa, 0x22, 0xce, 0xc1, 0x49, 0xf8, 0x02, 0xe1}}};
// {D30B19EB-A9DF-4D41-99EC-1247E1AED5D6}
inline constexpr ntl::wfp::filter_key<quic_layer_v4>
    enforcement_filter_key_v4{
        GUID{0xd30b19eb, 0xa9df, 0x4d41,
             {0x99, 0xec, 0x12, 0x47, 0xe1, 0xae, 0xd5, 0xd6}}};
// {199DC8F2-AB62-410A-A277-94215760F4EC}
inline constexpr ntl::wfp::filter_key<quic_layer_v6>
    enforcement_filter_key_v6{
        GUID{0x199dc8f2, 0xab62, 0x410a,
             {0xa2, 0x77, 0x94, 0x21, 0x57, 0x60, 0xf4, 0xec}}};

} // namespace wfp_kernel_browser_https_inspection
