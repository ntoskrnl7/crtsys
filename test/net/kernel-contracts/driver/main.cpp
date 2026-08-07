#include <ndis.h>
#include <ntddk.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>

#include <ntl/device_endpoint>
#include <ntl/driver>
#include <ntl/except>
#include <ntl/ioctl>
#include <ntl/net/http3/msquic_backend>
#include <ntl/net/kernel/all>
#include <ntl/net/kernel/content_codecs>
#include <ntl/net/kernel/msquic>
#include <ntl/wfp/classify>
#include <ntl/wfp/injection>
#include <ntl/wfp/transparent_udp_proxy>

#include "contract.hpp"

namespace {

ntl::status verify_wfp_classification_contract() noexcept {
  FWPS_FILTER2 clear_after_permit{};
  clear_after_permit.action.type = FWP_ACTION_CALLOUT_TERMINATING;
  clear_after_permit.flags = FWPS_FILTER_FLAG_CLEAR_ACTION_RIGHT;

  FWPS_CLASSIFY_OUT0 output{};
  output.actionType = FWP_ACTION_BLOCK;
  ntl::wfp::detail::apply_decision(
      ntl::wfp::detail::classification_action::permit,
      &clear_after_permit, output);
  if (output.actionType != FWP_ACTION_BLOCK)
    return STATUS_ASSERTION_FAILURE;

  output = {};
  output.actionType = FWP_ACTION_PERMIT;
  ntl::wfp::detail::apply_decision(
      ntl::wfp::detail::classification_action::block, nullptr, output);
  if (output.actionType != FWP_ACTION_BLOCK ||
      (output.rights & FWPS_RIGHT_ACTION_WRITE) != 0)
    return STATUS_ASSERTION_FAILURE;

  output = {};
  output.rights = FWPS_RIGHT_ACTION_WRITE;
  ntl::wfp::detail::apply_decision(
      ntl::wfp::detail::classification_action::permit,
      &clear_after_permit, output);
  if (output.actionType != FWP_ACTION_PERMIT ||
      (output.rights & FWPS_RIGHT_ACTION_WRITE) != 0)
    return STATUS_ASSERTION_FAILURE;

  output = {};
  output.rights = FWPS_RIGHT_ACTION_WRITE;
  ntl::wfp::detail::apply_decision(
      ntl::wfp::detail::classification_action::block_and_absorb,
      nullptr, output);
  if (output.actionType != FWP_ACTION_BLOCK ||
      (output.rights & FWPS_RIGHT_ACTION_WRITE) != 0 ||
      (output.flags & FWPS_CLASSIFY_OUT_FLAG_ABSORB) == 0)
    return STATUS_ASSERTION_FAILURE;

  FWPS_FILTER2 inspection{};
  inspection.action.type = FWP_ACTION_CALLOUT_INSPECTION;
  output = {};
  output.rights = FWPS_RIGHT_ACTION_WRITE;
  ntl::wfp::detail::apply_decision(
      ntl::wfp::detail::classification_action::block,
      &inspection, output);
  if (output.actionType != FWP_ACTION_CONTINUE)
    return STATUS_ASSERTION_FAILURE;

  FWPS_FILTER2 terminating{};
  terminating.action.type = FWP_ACTION_CALLOUT_TERMINATING;
  output = {};
  output.rights = FWPS_RIGHT_ACTION_WRITE;
  ntl::wfp::detail::apply_decision(
      ntl::wfp::detail::classification_action::continue_classification,
      &terminating, output);
  if (output.actionType != FWP_ACTION_BLOCK ||
      (output.rights & FWPS_RIGHT_ACTION_WRITE) != 0)
    return STATUS_ASSERTION_FAILURE;

  FWPS_STREAM_DATA0 data{};
  data.dataLength = 12;
  FWPS_STREAM_CALLOUT_IO_PACKET0 packet{};
  packet.streamData = &data;

  output = {};
  output.rights = FWPS_RIGHT_ACTION_WRITE;
  ntl::wfp::detail_apply_stream_result(
      ntl::wfp::stream_result::need_more(32), nullptr, &packet, output);
  if (output.actionType != FWP_ACTION_NONE ||
      packet.streamAction != FWPS_STREAM_ACTION_NEED_MORE_DATA ||
      packet.countBytesRequired != 32)
    return STATUS_ASSERTION_FAILURE;

  output = {};
  output.rights = FWPS_RIGHT_ACTION_WRITE;
  ntl::wfp::detail_apply_stream_result(
      ntl::wfp::stream_result::permit(99), nullptr, &packet, output);
  if (output.actionType != FWP_ACTION_PERMIT ||
      packet.streamAction != FWPS_STREAM_ACTION_NONE ||
      packet.countBytesEnforced != data.dataLength)
    return STATUS_ASSERTION_FAILURE;

  output = {};
  output.rights = FWPS_RIGHT_ACTION_WRITE;
  ntl::wfp::detail_apply_stream_result(
      ntl::wfp::stream_result::defer(), nullptr, &packet, output);
  if (output.actionType != FWP_ACTION_BLOCK ||
      packet.streamAction != FWPS_STREAM_ACTION_NONE ||
      packet.countBytesEnforced != data.dataLength)
    return STATUS_ASSERTION_FAILURE;

  data.flags = FWPS_STREAM_FLAG_RECEIVE;
  output = {};
  output.rights = FWPS_RIGHT_ACTION_WRITE;
  ntl::wfp::detail_apply_stream_result(
      ntl::wfp::stream_result::defer(), nullptr, &packet, output);
  if (output.actionType != FWP_ACTION_NONE ||
      packet.streamAction != FWPS_STREAM_ACTION_DEFER)
    return STATUS_ASSERTION_FAILURE;

  output = {};
  output.rights = FWPS_RIGHT_ACTION_WRITE;
  output.flags = FWPS_CLASSIFY_OUT_FLAG_BUFFER_LIMIT_REACHED;
  ntl::wfp::detail_apply_stream_result(
      ntl::wfp::stream_result::need_more(32), nullptr, &packet, output);
  return output.actionType == FWP_ACTION_BLOCK &&
                 packet.streamAction == FWPS_STREAM_ACTION_NONE &&
                 packet.countBytesEnforced == data.dataLength
             ? ntl::status::ok()
             : ntl::status{STATUS_ASSERTION_FAILURE};
}

static_assert(requires(ntl::net::kernel::msquic_listener &value) {
  value.close();
  value.wait_closed();
});
static_assert(requires(ntl::net::http3::msquic_backend::connection &value) {
  value.drain_exact();
});
static_assert(requires(ntl::net::kernel::bounded_wait_set<4> &value,
                       PVOID object, PLARGE_INTEGER timeout) {
  value.try_add_unique(object);
  value.wait_any(Executive, KernelMode, FALSE, timeout);
});
static_assert(requires(
    const ntl::net::kernel::schannel_certificate_store_ref &certificate,
    ntl::net::kernel::msquic_configuration &configuration,
    ntl::net::kernel::schannel &schannel) {
  schannel.try_client(
      {.borrowed_certificate = &certificate});
  schannel.try_server(certificate);
  schannel.close();
  configuration.load_client_certificate(certificate.sha1_thumbprint, "MY",
                                        true, true);
});
static_assert(requires(
    ntl::net::kernel::tls_stream &stream,
    ntl::net::kernel::schannel_credentials &credentials,
    std::shared_ptr<ntl::net::kernel::schannel_peer_certificate_policy> policy,
    std::span<std::byte> destination) {
  stream.handshake_client(
      credentials, L"kernel.example",
      ntl::net::kernel::tls_client_handshake_options{
          .peer_certificate_policy = policy});
  stream.handshake_server(
      credentials, ntl::net::kernel::tls_server_handshake_options{
                       .require_client_certificate = true,
                       .client_certificate_policy = policy});
  stream.read_some_borrowed(
      destination,
      ntl::net::read_options{
          .timeout = (std::chrono::milliseconds::max)()});
  stream.cancel_pending_read();
  stream.close();
});
static_assert(std::is_same_v<
              decltype(std::declval<const ntl::net::kernel::tls_stream &>()
                           .negotiated_application_protocol()),
              std::string>);
static_assert(ntl::net::kernel::detail::transport_half_close_completed(
    STATUS_DELETE_PENDING));
static_assert(ntl::net::kernel::detail::transport_half_close_completed(
    STATUS_CONNECTION_DISCONNECTED));
static_assert(!ntl::net::kernel::detail::transport_half_close_completed(
    STATUS_ACCESS_DENIED));

using inspect_ioctl =
    ntl::ioctl_from_contract<ntl_net_kernel_sample::inspect_ioctl_contract>;

struct ioctl_router_request {
  std::uint32_t value = 0;
};

struct ioctl_router_reply {
  std::uint32_t value = 0;
};

using ioctl_router_round_trip =
    ntl::ioctl<FILE_DEVICE_UNKNOWN, 0x9f0, METHOD_BUFFERED,
               FILE_READ_DATA | FILE_WRITE_DATA, ioctl_router_request,
               ioctl_router_reply>;
using ioctl_router_failure =
    ntl::ioctl<FILE_DEVICE_UNKNOWN, 0x9f1, METHOD_BUFFERED,
               FILE_READ_DATA | FILE_WRITE_DATA, void,
               ioctl_router_reply>;
using ioctl_router_late =
    ntl::ioctl<FILE_DEVICE_UNKNOWN, 0x9f2, METHOD_BUFFERED,
               FILE_READ_DATA | FILE_WRITE_DATA, void, void>;
using ioctl_router_unknown =
    ntl::ioctl<FILE_DEVICE_UNKNOWN, 0x9f3, METHOD_BUFFERED,
               FILE_READ_DATA | FILE_WRITE_DATA, void, void>;

ntl::status verify_ioctl_router_contract() noexcept {
  ntl::device_control::ioctl_router<4> router;
  ntl::status result = router.add<ioctl_router_round_trip>(
      [](const ioctl_router_request &request,
         ioctl_router_reply &reply) noexcept -> ntl::status {
        reply.value = request.value + 1;
        return ntl::status::ok();
      });
  if (!result.is_ok())
    return result;

  result = router.add<ioctl_router_failure>(
      [](ioctl_router_reply &reply) noexcept -> ntl::status {
        reply.value = 0xffffffffu;
        return STATUS_ACCESS_DENIED;
      });
  if (!result.is_ok())
    return result;

  const ntl::status duplicate = router.add<ioctl_router_round_trip>(
      [](const ioctl_router_request &,
         ioctl_router_reply &) noexcept -> ntl::status {
        return ntl::status::ok();
      });
  if (static_cast<NTSTATUS>(duplicate) != STATUS_OBJECT_NAME_COLLISION)
    return STATUS_DATA_ERROR;

  alignas(ioctl_router_reply)
      std::array<std::byte, sizeof(ioctl_router_reply)> buffer{};
  const ioctl_router_request request{41};
  std::memcpy(buffer.data(), &request, sizeof(request));
  ntl::device_control::in_buffer input{buffer.data(), sizeof(request)};
  ntl::device_control::out_buffer output{buffer.data(), buffer.size()};
  result = router.dispatch(ioctl_router_round_trip::control_code(), input,
                           output);
  if (!result.is_ok() || output.size != sizeof(ioctl_router_reply) ||
      reinterpret_cast<const ioctl_router_reply *>(buffer.data())->value !=
          42)
    return STATUS_DATA_ERROR;

  // First dispatch seals the immutable route table.
  const ntl::status late = router.add<ioctl_router_late>(
      []() noexcept -> ntl::status { return ntl::status::ok(); });
  if (static_cast<NTSTATUS>(late) != STATUS_INVALID_DEVICE_STATE)
    return STATUS_DATA_ERROR;

  output = {buffer.data(), buffer.size()};
  result = router.dispatch(ioctl_router_unknown::control_code(), input,
                           output);
  if (static_cast<NTSTATUS>(result) != STATUS_INVALID_DEVICE_REQUEST ||
      output.size != 0)
    return STATUS_DATA_ERROR;

  input = {buffer.data(), sizeof(request) - 1};
  output = {buffer.data(), buffer.size()};
  result = router.dispatch(ioctl_router_round_trip::control_code(), input,
                           output);
  if (static_cast<NTSTATUS>(result) != STATUS_BUFFER_TOO_SMALL ||
      output.size != 0)
    return STATUS_DATA_ERROR;

  std::array<std::byte, sizeof(ioctl_router_request) + 1> oversized{};
  input = {oversized.data(), oversized.size()};
  output = {buffer.data(), buffer.size()};
  result = router.dispatch(ioctl_router_round_trip::control_code(), input,
                           output);
  if (static_cast<NTSTATUS>(result) != STATUS_INFO_LENGTH_MISMATCH ||
      output.size != 0)
    return STATUS_DATA_ERROR;

  input = {nullptr, 0};
  output = {buffer.data(), buffer.size()};
  result = router.dispatch(ioctl_router_failure::control_code(), input,
                           output);
  if (static_cast<NTSTATUS>(result) != STATUS_ACCESS_DENIED ||
      output.size != 0)
    return STATUS_DATA_ERROR;

  router.stop_accepting();
  output = {buffer.data(), buffer.size()};
  result = router.dispatch(ioctl_router_failure::control_code(), input,
                           output);
  if (static_cast<NTSTATUS>(result) != STATUS_DELETE_PENDING ||
      output.size != 0)
    return STATUS_DATA_ERROR;
  return router.drain();
}

struct device_extension {};

struct executor_probe {
  executor_probe() noexcept {
    KeInitializeEvent(&completed, NotificationEvent, FALSE);
  }
  KEVENT completed{};
  std::atomic<std::uint32_t> calls{0};
};

void run_executor_probe(void *context) noexcept {
  auto &probe = *static_cast<executor_probe *>(context);
  probe.calls.fetch_add(1, std::memory_order_release);
  KeSetEvent(&probe.completed, IO_NO_INCREMENT, FALSE);
}

struct executor_lifetime_witness {
  executor_lifetime_witness() noexcept {
    KeInitializeEvent(&started, NotificationEvent, FALSE);
    KeInitializeEvent(&proceed, NotificationEvent, FALSE);
    KeInitializeEvent(&completed, NotificationEvent, FALSE);
  }

  KEVENT started{};
  KEVENT proceed{};
  KEVENT completed{};
  std::atomic<bool> owner_destroyed{false};
  std::atomic<std::uint32_t> calls{0};
};

struct executor_lifetime_probe {
  explicit executor_lifetime_probe(
      std::shared_ptr<executor_lifetime_witness> value) noexcept
      : witness(std::move(value)) {}
  ~executor_lifetime_probe() {
    witness->owner_destroyed.store(true, std::memory_order_release);
  }

  std::shared_ptr<executor_lifetime_witness> witness;
  std::optional<ntl::net::kernel::executor> callback_owner;
};

struct wsk_provider_close_race_probe {
  explicit wsk_provider_close_race_probe(
      ntl::net::kernel::wsk_provider &provider) noexcept
      : provider(&provider) {
    KeInitializeEvent(&started, NotificationEvent, FALSE);
    KeInitializeEvent(&completed, NotificationEvent, FALSE);
  }

  ntl::net::kernel::wsk_provider *provider = nullptr;
  KEVENT started{};
  KEVENT completed{};
  std::atomic<bool> stop{false};
  std::atomic<std::uint32_t> observations{0};
};

void run_wsk_provider_close_race(void *context) noexcept {
  auto &probe = *static_cast<wsk_provider_close_race_probe *>(context);
  KeSetEvent(&probe.started, IO_NO_INCREMENT, FALSE);
  while (!probe.stop.load(std::memory_order_acquire)) {
    if (static_cast<bool>(*probe.provider))
      probe.observations.fetch_add(1, std::memory_order_relaxed);
  }
  // The facade must remain safely observable after the concurrent close.
  (void)static_cast<bool>(*probe.provider);
  KeSetEvent(&probe.completed, IO_NO_INCREMENT, FALSE);
}

struct wsk_datagram_relay_close_race_probe {
  explicit wsk_datagram_relay_close_race_probe(
      ntl::net::kernel::wsk_datagram_relay &relay) noexcept
      : relay(&relay) {
    KeInitializeEvent(&started, NotificationEvent, FALSE);
    KeInitializeEvent(&completed, NotificationEvent, FALSE);
  }

  ntl::net::kernel::wsk_datagram_relay *relay = nullptr;
  KEVENT started{};
  KEVENT completed{};
  std::atomic<bool> stop{false};
  std::atomic<std::uint32_t> observations{0};
};

void run_wsk_datagram_relay_close_race(void *context) noexcept {
  auto &probe =
      *static_cast<wsk_datagram_relay_close_race_probe *>(context);
  KeSetEvent(&probe.started, IO_NO_INCREMENT, FALSE);
  while (!probe.stop.load(std::memory_order_acquire)) {
    (void)probe.relay->local_port();
    (void)probe.relay->statistics();
    (void)static_cast<bool>(*probe.relay);
    probe.observations.fetch_add(1, std::memory_order_relaxed);
  }
  (void)probe.relay->statistics();
  KeSetEvent(&probe.completed, IO_NO_INCREMENT, FALSE);
}

struct msquic_facade_close_race_probe {
  msquic_facade_close_race_probe(
      ntl::net::kernel::msquic_provider &provider,
      ntl::net::kernel::msquic_registration &registration,
      ntl::net::kernel::msquic_configuration &configuration,
      ntl::net::kernel::msquic_listener &listener) noexcept
      : provider(&provider), registration(&registration),
        configuration(&configuration), listener(&listener) {
    KeInitializeEvent(&started, NotificationEvent, FALSE);
    KeInitializeEvent(&completed, NotificationEvent, FALSE);
  }

  ntl::net::kernel::msquic_provider *provider = nullptr;
  ntl::net::kernel::msquic_registration *registration = nullptr;
  ntl::net::kernel::msquic_configuration *configuration = nullptr;
  ntl::net::kernel::msquic_listener *listener = nullptr;
  KEVENT started{};
  KEVENT completed{};
  std::atomic<bool> stop{false};
  std::atomic<std::uint32_t> observations{0};
};

void run_msquic_facade_close_race(void *context) noexcept {
  auto &probe = *static_cast<msquic_facade_close_race_probe *>(context);
  KeSetEvent(&probe.started, IO_NO_INCREMENT, FALSE);
  while (!probe.stop.load(std::memory_order_acquire)) {
    (void)static_cast<bool>(*probe.provider);
    (void)static_cast<bool>(*probe.registration);
    auto retained = probe.configuration->make_connection_context();
    (void)probe.listener->local_address();
    probe.observations.fetch_add(1, std::memory_order_relaxed);
  }
  KeSetEvent(&probe.completed, IO_NO_INCREMENT, FALSE);
}

class rejecting_msquic_listener_sink final
    : public ntl::net::kernel::msquic_listener_sink {
public:
  ntl::status on_connection(
      ntl::net::http3::msquic_backend::borrowed_accepted_connection)
      noexcept override {
    return STATUS_CONNECTION_REFUSED;
  }
};

struct schannel_close_race_probe {
  schannel_close_race_probe(
      ntl::net::kernel::schannel &owner,
      ntl::net::kernel::schannel_credentials &credentials) noexcept
      : owner(&owner), credentials(&credentials) {
    KeInitializeEvent(&started, NotificationEvent, FALSE);
    KeInitializeEvent(&completed, NotificationEvent, FALSE);
  }

  ntl::net::kernel::schannel *owner = nullptr;
  ntl::net::kernel::schannel_credentials *credentials = nullptr;
  KEVENT started{};
  KEVENT completed{};
  std::atomic<bool> stop{false};
  std::atomic<std::uint32_t> observations{0};
};

void run_schannel_close_race(void *context) noexcept {
  auto &probe = *static_cast<schannel_close_race_probe *>(context);
  KeSetEvent(&probe.started, IO_NO_INCREMENT, FALSE);
  while (!probe.stop.load(std::memory_order_acquire)) {
    ntl::net::kernel::schannel_credentials copy = *probe.credentials;
    (void)static_cast<bool>(copy);
    (void)probe.credentials->role();
    (void)probe.owner->live_credentials();
    probe.observations.fetch_add(1, std::memory_order_relaxed);
  }
  KeSetEvent(&probe.completed, IO_NO_INCREMENT, FALSE);
}

struct schannel_cleanup_reentry_probe {
  explicit schannel_cleanup_reentry_probe(
      ntl::net::kernel::schannel &owner) noexcept
      : item(&schannel_cleanup_reentry_probe::run, this), owner(&owner) {
    KeInitializeEvent(&completed, NotificationEvent, FALSE);
  }

  static void run(void *context) noexcept {
    auto &probe = *static_cast<schannel_cleanup_reentry_probe *>(context);
    probe.result = probe.owner->close();
    KeSetEvent(&probe.completed, IO_NO_INCREMENT, FALSE);
  }

  ntl::net::kernel::passive_cleanup_item item;
  ntl::net::kernel::schannel *owner = nullptr;
  KEVENT completed{};
  ntl::status result{STATUS_PENDING};
};

struct kernel_tls_stream_close_race_probe {
  explicit kernel_tls_stream_close_race_probe(
      ntl::net::kernel::tls_stream &stream) noexcept : stream(&stream) {
    KeInitializeEvent(&started, NotificationEvent, FALSE);
    KeInitializeEvent(&completed, NotificationEvent, FALSE);
  }

  ntl::net::kernel::tls_stream *stream = nullptr;
  KEVENT started{};
  KEVENT completed{};
  std::atomic<bool> stop{false};
  std::atomic<std::uint32_t> observations{0};
};

void run_kernel_tls_stream_close_race(void *context) noexcept {
  auto &probe =
      *static_cast<kernel_tls_stream_close_race_probe *>(context);
  KeSetEvent(&probe.started, IO_NO_INCREMENT, FALSE);
  while (!probe.stop.load(std::memory_order_acquire)) {
    (void)static_cast<bool>(*probe.stream);
    (void)probe.stream->is_handshaken();
    (void)probe.stream->is_write_closed();
    (void)probe.stream->received_close_notify();
    (void)probe.stream->last_close_stage();
    probe.observations.fetch_add(1, std::memory_order_relaxed);
  }
  KeSetEvent(&probe.completed, IO_NO_INCREMENT, FALSE);
}

void run_executor_lifetime_probe(executor_lifetime_probe &probe) noexcept {
  auto witness = probe.witness;
  KeSetEvent(&witness->started, IO_NO_INCREMENT, FALSE);
  (void)KeWaitForSingleObject(
      &witness->proceed, Executive, KernelMode, FALSE, nullptr);
  probe.callback_owner.reset();
  witness->calls.fetch_add(1, std::memory_order_release);
  KeSetEvent(&witness->completed, IO_NO_INCREMENT, FALSE);
}

class tls_observer final : public ntl::net::tls_client_hello_observer {
public:
  ntl::status on_server_name(std::string_view) noexcept override {
    flags |= ntl_net_kernel_sample::result_flag::server_name;
    ++fields;
    return ntl::status::ok();
  }
  ntl::status on_application_protocol(std::string_view) noexcept override {
    ++fields;
    return ntl::status::ok();
  }
  std::uint32_t fields = 0;
  std::uint32_t flags = 0;
};

struct task_lifetime_probe {
  task_lifetime_probe(KEVENT &completed, std::atomic<bool> &passive) noexcept
      : completed(&completed), passive(&passive) {}
  ~task_lifetime_probe() {
    passive->store(KeGetCurrentIrql() == PASSIVE_LEVEL,
                   std::memory_order_release);
    KeSetEvent(completed, IO_NO_INCREMENT, FALSE);
  }

  KEVENT *completed = nullptr;
  std::atomic<bool> *passive = nullptr;
};

struct task_resume_slot {
  std::coroutine_handle<> continuation{};
};

struct suspend_task_for_lifetime_test {
  std::shared_ptr<task_resume_slot> slot;
  bool await_ready() const noexcept { return false; }
  void await_suspend(std::coroutine_handle<> continuation) const noexcept {
    slot->continuation = continuation;
  }
  void await_resume() const noexcept {}
};

ntl::net::kernel::waitable_status_task finish_task_at_passive(
    std::shared_ptr<task_lifetime_probe> probe,
    std::shared_ptr<task_resume_slot> slot) {
  co_await suspend_task_for_lifetime_test{std::move(slot)};
  (void)probe;
  co_return ntl::status::ok();
}

class qpack_observer final : public ntl::net::http3::qpack_field_sink {
public:
  ntl::status on_field(ntl::net::http3::qpack_field_view) noexcept override {
    ++fields;
    return ntl::status::ok();
  }
  std::uint32_t fields = 0;
};

class dynamic_qpack_observer final : public ntl::net::http3::inspection_sink {
public:
  ntl::status on_headers(
      std::uint64_t,
      std::span<const ntl::net::http3::header_field> values) noexcept override {
    fields += static_cast<std::uint32_t>(values.size());
    if (values.size() != 1 || values[0].name != "x" || values[0].value != "y")
      return STATUS_DATA_ERROR;
    return ntl::status::ok();
  }
  ntl::status on_data(std::uint64_t,
                      ntl::net::scatter_view bytes) noexcept override {
    data += static_cast<std::uint32_t>(bytes.size());
    return ntl::status::ok();
  }
  std::uint32_t fields = 0;
  std::uint32_t data = 0;
};

class webtransport_contract_backend final
    : public ntl::net::quic::transport_backend {
public:
  ntl::net::quic::backend_capabilities capabilities() const noexcept override {
    return {.available = true,
            .tls13_termination = true,
            .qpack_dynamic_table = true,
            .bidirectional_streams = true,
            .unidirectional_streams = true,
            .quic_datagrams = true,
            .reliable_reset_at = true,
            .extended_connect = true,
            .webtransport = true};
  }
  ntl::status run_borrowed(
      ntl::net::quic::backend_sink &) noexcept override {
    return ntl::status::ok();
  }
  ntl::status write_stream(std::uint64_t stream_id,
                           ntl::net::scatter_view plaintext,
                           bool final) noexcept override {
    if ((!plaintext && !final) || stream_id == 0)
      return STATUS_INVALID_PARAMETER;
    bytes += plaintext.size();
    ++writes;
    return ntl::status::ok();
  }
  ntl::status
  open_bidirectional_stream(std::uint64_t &stream_id) noexcept override {
    stream_id = next_bidirectional;
    next_bidirectional += 4;
    return ntl::status::ok();
  }
  ntl::status open_request_stream(std::uint64_t &stream_id) noexcept override {
    stream_id = 4;
    return ntl::status::ok();
  }
  ntl::status
  open_unidirectional_stream(std::uint64_t &stream_id) noexcept override {
    stream_id = next_unidirectional;
    next_unidirectional += 4;
    return ntl::status::ok();
  }
  ntl::status
  send_datagram(ntl::net::scatter_view plaintext) noexcept override {
    auto parsed = ntl::net::http::http3_datagram_view::parse(plaintext, {1024});
    if (!parsed || parsed->request_stream_id() != 4)
      return parsed ? ntl::status{STATUS_DATA_ERROR} : parsed.status();
    datagram_bytes += parsed->payload().size();
    return ntl::status::ok();
  }
  ntl::status reset_stream_at(std::uint64_t stream_id, std::uint64_t,
                              std::uint64_t reliable_size) noexcept override {
    if (stream_id == 0 || reliable_size == 0)
      return STATUS_INVALID_PARAMETER;
    ++resets;
    return ntl::status::ok();
  }
  void stop() noexcept override { stopped = true; }
  ntl::status drain() noexcept override {
    return stopped ? ntl::status::ok()
                   : ntl::status{STATUS_INVALID_DEVICE_STATE};
  }

  std::uint64_t next_bidirectional = 8;
  std::uint64_t next_unidirectional = 2;
  std::size_t bytes = 0;
  std::size_t datagram_bytes = 0;
  std::uint32_t writes = 0;
  std::uint32_t resets = 0;
  bool stopped = false;
};

class deferred_transport_backend final
    : public ntl::net::io::transport_backend {
public:
  deferred_transport_backend() noexcept {
    KeInitializeSpinLock(&lock_);
    KeInitializeEvent(&drained_, NotificationEvent, TRUE);
  }

  ~deferred_transport_backend() override {
    NT_ASSERT(pending_.load(std::memory_order_acquire) == 0);
  }

  ntl::net::io::transport_capabilities capabilities() const noexcept override {
    return {.available = true,
            .full_duplex = true,
            .half_close = true,
            .cancellation = false,
            .message_boundaries = false,
            .plaintext = true};
  }

  ntl::status start_borrowed(
      ntl::net::io::transport_sink &sink) noexcept override {
    KIRQL old_irql = 0;
    KeAcquireSpinLock(&lock_, &old_irql);
    if (started_ || stopping_) {
      KeReleaseSpinLock(&lock_, old_irql);
      return STATUS_INVALID_DEVICE_STATE;
    }
    sink_ = &sink;
    started_ = true;
    KeReleaseSpinLock(&lock_, old_irql);
    return ntl::status::ok();
  }

  ntl::status write(std::uint64_t operation_id, ntl::net::scatter_view bytes,
                    bool final) noexcept override {
    if (operation_id == 0 || (!bytes && !final) ||
        bytes.size() > maximum_write_size)
      return STATUS_INVALID_PARAMETER;

    auto operation = ntl::try_make_pool<completion_operation>(
        ntl::pool_kind::nonpaged, ntl::pool_option::none, operation_tag, this,
        operation_id, bytes.size());
    if (!operation)
      return operation.status();

    KIRQL old_irql = 0;
    KeAcquireSpinLock(&lock_, &old_irql);
    if (!started_ || stopping_ || !sink_) {
      KeReleaseSpinLock(&lock_, old_irql);
      return STATUS_DELETE_PENDING;
    }
    pending_.fetch_add(1, std::memory_order_relaxed);
    writes_.fetch_add(1, std::memory_order_relaxed);
    bytes_.fetch_add(bytes.size(), std::memory_order_relaxed);
    if (final)
      finals_.fetch_add(1, std::memory_order_relaxed);
    KeClearEvent(&drained_);
    completion_operation *const raw = operation->release();
    KeReleaseSpinLock(&lock_, old_irql);

#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable : 4996)
#endif
    ExQueueWorkItem(&raw->work, DelayedWorkQueue);
#ifdef _MSC_VER
#pragma warning(pop)
#endif
    return ntl::status::ok();
  }

  ntl::status cancel(std::uint64_t) noexcept override {
    return STATUS_NOT_SUPPORTED;
  }

  ntl::status deliver(std::span<const std::byte> bytes,
                      bool final = false) noexcept {
    ntl::net::io::transport_sink *sink = nullptr;
    KIRQL old_irql = 0;
    KeAcquireSpinLock(&lock_, &old_irql);
    if (started_ && !stopping_)
      sink = sink_;
    KeReleaseSpinLock(&lock_, old_irql);
    if (!sink)
      return STATUS_DELETE_PENDING;
    return sink->on_receive(ntl::net::scatter_view::from_contiguous(bytes),
                            final);
  }

  void stop() noexcept override {
    KIRQL old_irql = 0;
    KeAcquireSpinLock(&lock_, &old_irql);
    stopping_ = true;
    KeReleaseSpinLock(&lock_, old_irql);
  }

  ntl::status drain() noexcept override {
    if (KeGetCurrentIrql() != PASSIVE_LEVEL)
      return STATUS_INVALID_DEVICE_STATE;
    stop();
    return KeWaitForSingleObject(&drained_, Executive, KernelMode, FALSE,
                                 nullptr);
  }

  std::uint32_t writes() const noexcept {
    return writes_.load(std::memory_order_acquire);
  }
  std::size_t bytes() const noexcept {
    return bytes_.load(std::memory_order_acquire);
  }
  std::uint32_t finals() const noexcept {
    return finals_.load(std::memory_order_acquire);
  }

private:
  static constexpr std::size_t maximum_write_size = 128 * 1024;
  static constexpr ULONG operation_tag = ntl::pool_tag("dAtN");

  struct completion_operation {
    completion_operation(deferred_transport_backend *owner_value,
                         std::uint64_t id,
                         std::size_t transferred_value) noexcept
        : owner(owner_value), operation_id(id), transferred(transferred_value) {
#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable : 4996)
#endif
      ExInitializeWorkItem(&work, &completion_operation::complete, this);
#ifdef _MSC_VER
#pragma warning(pop)
#endif
    }

    static void complete(void *context) noexcept {
      auto *const operation = static_cast<completion_operation *>(context);
      deferred_transport_backend *const owner = operation->owner;
      ntl::net::io::transport_sink *sink = nullptr;
      KIRQL old_irql = 0;
      KeAcquireSpinLock(&owner->lock_, &old_irql);
      sink = owner->sink_;
      KeReleaseSpinLock(&owner->lock_, old_irql);

      if (sink) {
        sink->on_write_complete(operation->operation_id, STATUS_SUCCESS,
                                operation->transferred);
      }

      if (owner->pending_.fetch_sub(1, std::memory_order_acq_rel) == 1)
        KeSetEvent(&owner->drained_, IO_NO_INCREMENT, FALSE);
      ntl::pool_deleter<completion_operation>{
          operation_tag, ntl::pool_kind::nonpaged}(operation);
    }

    deferred_transport_backend *owner = nullptr;
    std::uint64_t operation_id = 0;
    std::size_t transferred = 0;
    WORK_QUEUE_ITEM work{};
  };

  KSPIN_LOCK lock_{};
  KEVENT drained_{};
  ntl::net::io::transport_sink *sink_ = nullptr;
  std::atomic<std::uint32_t> pending_{0};
  std::atomic<std::uint32_t> writes_{0};
  std::atomic<std::uint32_t> finals_{0};
  std::atomic<std::size_t> bytes_{0};
  bool started_ = false;
  bool stopping_ = false;
};

using kernel_status_task = ntl::net::kernel::waitable_status_task;

template <class T>
concept individually_awaitable = requires(T value) {
  value.operator co_await();
};

template <class T>
concept individually_waitable = requires(T &value) {
  value.wait();
};

template <class T>
concept has_synchronous_drain = requires(T &value) {
  value.drain();
};

static_assert(
    !has_synchronous_drain<ntl::net::io::async_transport_stream>);

static_assert(
    !individually_awaitable<ntl::net::kernel::bidirectional_status_task>);
static_assert(
    !individually_waitable<ntl::net::kernel::bidirectional_status_task>);

kernel_status_task stop_and_drain_at_owner_boundary(
    ntl::net::io::async_transport_stream &stream) {
  co_return co_await stream.stop_and_drain();
}

kernel_status_task cancel_and_drain_at_owner_boundary(
    ntl::net::async_byte_stream &stream) {
  co_return co_await stream.cancel_and_drain();
}

kernel_status_task validate_managed_transport_early_return(
    deferred_transport_backend &backend) {
  co_return co_await ntl::net::io::with_async_transport_borrowed(
      backend, 4,
      [](std::shared_ptr<ntl::net::io::async_transport_stream>)
          -> ntl::net::kernel::task<ntl::status> {
        co_return ntl::status{STATUS_ACCESS_DENIED};
      });
}

kernel_status_task validate_managed_tls_early_return(
    deferred_transport_backend &backend) {
  co_return co_await ntl::net::kernel::with_tls_connection_borrowed(
      backend, 4, {},
      [](std::shared_ptr<ntl::net::kernel::tls_stream>,
         std::shared_ptr<ntl::net::io::async_transport_stream>)
          -> ntl::net::kernel::task<ntl::status> {
        co_return ntl::status{STATUS_ACCESS_DENIED};
      });
}

struct join_resume_slot {
  struct awaiter {
    join_resume_slot *owner = nullptr;

    bool await_ready() const noexcept { return false; }
    bool await_suspend(std::coroutine_handle<> continuation) noexcept {
      owner->continuation = continuation;
      owner->started = true;
      return true;
    }
    void await_resume() const noexcept { owner->resumed = true; }
  };

  awaiter suspend() noexcept { return awaiter{this}; }

  void resume() noexcept {
    const auto next = std::exchange(continuation, {});
    if (next)
      next.resume();
  }

  std::coroutine_handle<> continuation{};
  bool started = false;
  bool resumed = false;
};

ntl::net::kernel::bidirectional_status_task
complete_join_branch(ntl::status result, std::uint32_t &completions) {
  ++completions;
  co_return result;
}

ntl::net::kernel::bidirectional_status_task
suspend_join_branch(join_resume_slot &slot, ntl::status result,
                    std::uint32_t &completions) {
  co_await slot.suspend();
  ++completions;
  co_return result;
}

bool expected_join_shutdown(ntl::status result) noexcept {
  const NTSTATUS value = static_cast<NTSTATUS>(result);
  return value == STATUS_END_OF_FILE || value == STATUS_CANCELLED;
}

kernel_status_task validate_bidirectional_join_contract(
    ntl_net_kernel_sample::inspect_reply &reply) {
  std::uint32_t completions = 0;
  std::uint32_t cancellations = 0;

  join_resume_slot expected_slot;
  const ntl::status expected =
      co_await ntl::net::kernel::join_bidirectional(
          complete_join_branch(STATUS_END_OF_FILE, completions),
          suspend_join_branch(expected_slot, STATUS_CANCELLED, completions),
          [&]() noexcept {
            ++cancellations;
            expected_slot.resume();
          },
          &expected_join_shutdown);
  if (!expected.is_ok() || !expected_slot.started || !expected_slot.resumed ||
      completions != 2 || cancellations != 1)
    co_return ntl::status{STATUS_DATA_ERROR};

  join_resume_slot error_slot;
  const ntl::status error =
      co_await ntl::net::kernel::join_bidirectional(
          complete_join_branch(STATUS_ACCESS_DENIED, completions),
          suspend_join_branch(error_slot, STATUS_CANCELLED, completions),
          [&]() noexcept {
            ++cancellations;
            error_slot.resume();
          },
          &expected_join_shutdown);
  if (error != STATUS_ACCESS_DENIED || !error_slot.started ||
      !error_slot.resumed || completions != 4 || cancellations != 2)
    co_return ntl::status{STATUS_DATA_ERROR};

  auto moved_from = complete_join_branch(STATUS_SUCCESS, completions);
  auto retained = std::move(moved_from);
  const ntl::status invalid =
      co_await ntl::net::kernel::join_bidirectional(
          std::move(moved_from), std::move(retained), []() noexcept {},
          &expected_join_shutdown);
  if (invalid != STATUS_INVALID_HANDLE || completions != 4)
    co_return ntl::status{STATUS_DATA_ERROR};

  reply.transformed[3] = static_cast<std::byte>(cancellations);
  co_return ntl::status::ok();
}

kernel_status_task wsk_round_trip(
    std::shared_ptr<ntl::net::io::async_transport_stream> stream,
                                  std::span<const std::byte> payload,
                                  ntl_net_kernel_sample::inspect_reply &reply) {
  const auto written = co_await stream->write(payload);
  if (!written.status.is_ok())
    co_return written.status;
  if (written.transferred != payload.size())
    co_return ntl::status{STATUS_DATA_ERROR};
  const ntl::status read = co_await stream->read_exactly_borrowed(
      std::span<std::byte>(reply.transformed).first(payload.size()),
      {.timeout = std::chrono::seconds(10)});
  if (!read.is_ok())
    co_return read;
  reply.content_size = static_cast<std::uint32_t>(payload.size());
  reply.field_count = static_cast<std::uint32_t>(written.transferred);
  reply.flags = ntl_net_kernel_sample::result_flag::wsk_round_trip;
  co_return ntl::status::ok();
}

kernel_status_task
wsk_listener_round_trip(ntl::net::io::async_transport_stream &stream,
                        std::span<const std::byte> expected,
                        ntl_net_kernel_sample::inspect_reply &reply) {
  const ntl::status read = co_await stream.read_exactly_borrowed(
      std::span<std::byte>(reply.transformed).first(expected.size()),
      {.timeout = std::chrono::seconds(10)});
  if (!read.is_ok())
    co_return read;
  if (!std::equal(expected.begin(), expected.end(), reply.transformed.begin()))
    co_return ntl::status{STATUS_DATA_ERROR};
  const auto written = co_await stream.write(expected);
  if (!written.status.is_ok())
    co_return written.status;
  if (written.transferred != expected.size())
    co_return ntl::status{STATUS_DATA_ERROR};
  const auto shutdown = co_await stream.shutdown_write();
  if (!shutdown.status.is_ok() || shutdown.transferred != 0)
    co_return shutdown.status.is_ok() ? ntl::status{STATUS_DATA_ERROR}
                                      : shutdown.status;
  reply.content_size = static_cast<std::uint32_t>(expected.size());
  reply.field_count = static_cast<std::uint32_t>(written.transferred);
  reply.flags = ntl_net_kernel_sample::result_flag::wsk_listener_round_trip;
  co_return ntl::status::ok();
}

kernel_status_task
wsk_tls_round_trip(
                   std::shared_ptr<ntl::net::io::async_transport_stream>
                       transport,
                   std::span<const std::byte> payload,
                   ntl::net::kernel::executor &executor,
                   ntl_net_kernel_sample::inspect_reply &reply) {
  reply.field_count = 1;
  class controlled_loopback_certificate_policy final
      : public ntl::net::kernel::schannel_peer_certificate_policy {
  public:
    ntl::status verify(
        const ntl::net::kernel::schannel_peer_certificate_chain &chain,
        std::wstring_view peer_name) noexcept override {
      return !chain.empty() && peer_name == L"kernel.example"
                 ? ntl::status::ok()
                 : ntl::status{STATUS_ACCESS_DENIED};
    }
  };
  auto peer_policy =
      std::make_shared<controlled_loopback_certificate_policy>();

  ntl::net::kernel::schannel schannel;
  auto credentials = schannel.try_client(
      {.manual_peer_validation = true,
       .use_default_client_certificate = false});
  if (!credentials)
    co_return credentials.status();
  reply.field_count = 2;
  auto created = ntl::net::kernel::tls_stream::try_create(
      transport, {.maximum_buffered_ciphertext = 256 * 1024,
                  .maximum_plaintext_record = 64 * 1024,
                  .receive_timeout = std::chrono::seconds(10)});
  if (!created)
    co_return created.status();
  auto stream = std::move(*created);
  reply.field_count = 3;
  const ntl::status handshaken = co_await stream.handshake_client(
      *credentials, L"kernel.example",
      {.application_protocols = {"h2"},
       .require_application_protocol = true,
       .peer_certificate_policy = peer_policy});
  if (!handshaken.is_ok())
    co_return handshaken;
  if (stream.negotiated_application_protocol() != "h2")
    co_return ntl::status{STATUS_DATA_ERROR};
  reply.field_count = 4;
  const auto written = co_await stream.write_all(payload);
  if (!written)
    co_return written.status();
  if (*written != payload.size())
    co_return ntl::status{STATUS_DATA_ERROR};

  reply.field_count = 5;
  std::size_t received = 0;
  while (received != payload.size()) {
    auto count = co_await stream.read_some_borrowed(
        std::span<std::byte>(reply.transformed)
            .subspan(received, payload.size() - received));
    if (!count)
      co_return count.status();
    if (*count == 0)
      co_return ntl::status{STATUS_END_OF_FILE};
    received += *count;
  }
  reply.field_count = 6;
  std::array<std::byte, 1> end_probe{};
  auto end = co_await stream.read_some_borrowed(end_probe);
  if (!end)
    co_return end.status();
  if (*end != 0 || !stream.received_close_notify())
    co_return ntl::status{STATUS_DATA_ERROR};
  // The peer close_notify was already consumed. Complete our close_notify
  // and the transport half-close before the owning transport is drained;
  // shutdown() alone intentionally leaves the socket open for more reads.
  const ntl::status closed = co_await stream.close();
  if (!closed.is_ok())
    co_return closed;
  const ntl::status duplicate_close = co_await stream.close();
  if (static_cast<NTSTATUS>(duplicate_close) !=
      static_cast<NTSTATUS>(closed))
    co_return ntl::status{STATUS_DATA_ERROR};
  kernel_tls_stream_close_race_probe race(stream);
  const ntl::status posted =
      executor.post_borrowed({&run_kernel_tls_stream_close_race, &race});
  if (!posted.is_ok())
    co_return posted;
  LARGE_INTEGER timeout{};
  timeout.QuadPart = -5LL * 10'000'000LL;
  NTSTATUS waited = KeWaitForSingleObject(
      &race.started, Executive, KernelMode, FALSE, &timeout);
  if (!NT_SUCCESS(waited)) {
    race.stop.store(true, std::memory_order_release);
    (void)KeWaitForSingleObject(
        &race.completed, Executive, KernelMode, FALSE, nullptr);
    co_return ntl::status{waited};
  }
  KIRQL previous = PASSIVE_LEVEL;
  KeRaiseIrql(DISPATCH_LEVEL, &previous);
  stream.abort();
  stream.abort();
  KeLowerIrql(previous);
  race.stop.store(true, std::memory_order_release);
  waited = KeWaitForSingleObject(
      &race.completed, Executive, KernelMode, FALSE, &timeout);
  if (!NT_SUCCESS(waited)) {
    (void)KeWaitForSingleObject(
        &race.completed, Executive, KernelMode, FALSE, nullptr);
    co_return ntl::status{waited};
  }
  if (race.observations.load(std::memory_order_acquire) == 0)
    co_return ntl::status{STATUS_INTERNAL_ERROR};
  reply.content_size = static_cast<std::uint32_t>(received);
  reply.field_count = static_cast<std::uint32_t>(*written);
  reply.flags = ntl_net_kernel_sample::result_flag::tls_round_trip;
  co_return ntl::status::ok();
}

kernel_status_task
deferred_write_sequence(ntl::net::io::async_transport_stream &stream,
                        std::span<const std::byte> large_payload) {
  constexpr std::array<std::byte, 9> h2_settings{};
  constexpr std::array<std::byte, 32> h2_headers{};
  for (const auto bytes : {std::span<const std::byte>(h2_settings),
                           std::span<const std::byte>(h2_headers)}) {
    const auto written = co_await stream.write(bytes);
    if (!written.status.is_ok())
      co_return written.status;
    if (written.transferred != bytes.size())
      co_return ntl::status{STATUS_DATA_ERROR};
  }

  constexpr std::size_t tls_record_payload = 16 * 1024;
  std::size_t offset = 0;
  while (offset != large_payload.size()) {
    const std::size_t count =
        (std::min)(tls_record_payload, large_payload.size() - offset);
    const auto written =
        co_await stream.write(large_payload.subspan(offset, count));
    if (!written.status.is_ok())
      co_return written.status;
    if (written.transferred != count)
      co_return ntl::status{STATUS_DATA_ERROR};
    offset += count;
  }
  const auto shutdown = co_await stream.shutdown_write();
  if (!shutdown.status.is_ok() || shutdown.transferred != 0)
    co_return shutdown.status.is_ok() ? ntl::status{STATUS_DATA_ERROR}
                                      : shutdown.status;
  const auto rejected = co_await stream.write(h2_settings);
  if (rejected.status != STATUS_CONNECTION_DISCONNECTED)
    co_return ntl::status{STATUS_DATA_ERROR};
  co_return ntl::status::ok();
}

struct read_sequence_probe {
  read_sequence_probe() noexcept {
    KeInitializeEvent(&progress, NotificationEvent, FALSE);
  }
  KEVENT progress{};
  std::atomic<std::uint32_t> waiting{0};
  std::atomic<std::uint32_t> resumed{0};
};

ntl::net::kernel::task<ntl::status>
read_one_from_child_task(ntl::net::async_byte_stream &stream,
                         std::byte expected) {
  std::array<std::byte, 1> byte{};
  const ntl::status read = co_await stream.read_exactly_borrowed(
      std::span<std::byte>(byte), {std::chrono::seconds(2)});
  if (!read.is_ok())
    co_return read;
  co_return byte[0] == expected ? ntl::status::ok()
                                : ntl::status{STATUS_DATA_ERROR};
}

kernel_status_task read_reawait_sequence(ntl::net::async_byte_stream &stream,
                                         read_sequence_probe &probe,
                                         std::uint32_t count) {
  for (std::uint32_t index = 0; index != count; ++index) {
    probe.waiting.store(index + 1, std::memory_order_release);
    KeSetEvent(&probe.progress, IO_NO_INCREMENT, FALSE);
    const ntl::status read = co_await read_one_from_child_task(
        stream, static_cast<std::byte>(index));
    probe.resumed.fetch_add(1, std::memory_order_relaxed);
    if (!read.is_ok())
      co_return read;
  }
  co_return ntl::status::ok();
}

kernel_status_task
transport_read_some_sequence(ntl::net::io::async_transport_stream &stream,
                             read_sequence_probe &probe,
                             std::uint32_t generations) {
  std::size_t expected_offset = 0;
  for (std::uint32_t generation = 0; generation != generations; ++generation) {
    probe.waiting.store(generation + 1, std::memory_order_release);
    KeSetEvent(&probe.progress, IO_NO_INCREMENT, FALSE);
    std::array<std::byte, 32> destination{};
    auto received = co_await stream.read_some_borrowed(
        destination, {.timeout = std::chrono::seconds(2)});
    probe.resumed.fetch_add(1, std::memory_order_relaxed);
    const std::size_t expected_count = (generation % 17) + 1;
    if (!received)
      co_return received.status();
    if (*received != expected_count)
      co_return ntl::status{STATUS_DATA_ERROR};
    for (std::size_t index = 0; index != *received; ++index) {
      const auto expected = static_cast<std::byte>(
          static_cast<unsigned char>((expected_offset + index) & 0xff));
      if (destination[index] != expected)
        co_return ntl::status{STATUS_DATA_ERROR};
    }
    expected_offset += *received;
  }
  co_return ntl::status::ok();
}

kernel_status_task read_then_stop_and_drain_from_resume_callback(
    ntl::net::io::async_transport_stream &stream,
    std::atomic<std::uint32_t> &completed) {
  std::array<std::byte, 1> byte{};
  auto received = co_await stream.read_some_borrowed(
      byte, {.timeout = std::chrono::seconds(2)});
  if (!received)
    co_return received.status();
  if (*received != byte.size() || byte[0] != std::byte{0x7a})
    co_return ntl::status{STATUS_DATA_ERROR};

  // The read continuation runs inside async_byte_stream's resume worker.  A
  // synchronous drain here used to wait for that same worker and deadlock.
  const ntl::status drained = co_await stream.stop_and_drain();
  if (!drained.is_ok())
    co_return drained;
  completed.store(1, std::memory_order_release);
  co_return ntl::status::ok();
}

kernel_status_task read_terminal_once(ntl::net::async_byte_stream &stream,
                                      NTSTATUS expected,
                                      std::atomic<std::uint32_t> &resumed) {
  std::array<std::byte, 2> bytes{};
  const ntl::status read = co_await stream.read_exactly_borrowed(
      std::span<std::byte>(bytes), {std::chrono::seconds(2)});
  resumed.fetch_add(1, std::memory_order_relaxed);
  co_return static_cast<NTSTATUS>(read) == expected
      ? ntl::status::ok()
      : ntl::status{STATUS_DATA_ERROR};
}

class byte_stream_race_action {
public:
  enum class kind : std::uint8_t { append, close, cancel };

  byte_stream_race_action(ntl::net::async_byte_stream &stream,
                          kind operation) noexcept
      : stream_(&stream), operation_(operation) {
    KeInitializeEvent(&completed_, NotificationEvent, FALSE);
#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable : 4996)
#endif
    ExInitializeWorkItem(&work_, &byte_stream_race_action::run, this);
#ifdef _MSC_VER
#pragma warning(pop)
#endif
  }

  void queue() noexcept {
#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable : 4996)
#endif
    ExQueueWorkItem(&work_, DelayedWorkQueue);
#ifdef _MSC_VER
#pragma warning(pop)
#endif
  }

  ntl::status wait() noexcept {
    LARGE_INTEGER timeout{};
    timeout.QuadPart = -5LL * 1000 * 1000 * 10;
    return KeWaitForSingleObject(&completed_, Executive, KernelMode, FALSE,
                                 &timeout);
  }

  ntl::status status() const noexcept { return status_; }

private:
  static void run(void *context) noexcept {
    auto &action = *static_cast<byte_stream_race_action *>(context);
    switch (action.operation_) {
    case kind::append: {
      constexpr std::array<std::byte, 1> byte{std::byte{0x5a}};
      action.status_ = action.stream_->append_received_data(
          ntl::net::scatter_view::from_contiguous(byte));
      break;
    }
    case kind::close:
      action.stream_->close();
      action.status_ = ntl::status::ok();
      break;
    case kind::cancel:
      action.stream_->cancel();
      action.status_ = ntl::status::ok();
      break;
    }
    KeSetEvent(&action.completed_, IO_NO_INCREMENT, FALSE);
  }

  ntl::net::async_byte_stream *stream_ = nullptr;
  kind operation_ = kind::append;
  WORK_QUEUE_ITEM work_{};
  KEVENT completed_{};
  ntl::status status_{STATUS_PENDING};
};

ntl::result<std::size_t>
uppercase_transform(void *, const ntl::net::transform_context &,
                    ntl::net::inspection::content_view input,
                    std::span<std::byte> output) noexcept {
  if (output.size() < input.size())
    return ntl::unexpected(STATUS_BUFFER_TOO_SMALL);
  const ntl::status copied = input.bytes().copy_to(output.first(input.size()));
  if (!copied.is_ok())
    return ntl::unexpected(copied);
  for (std::size_t index = 0; index != input.size(); ++index) {
    auto value = std::to_integer<unsigned char>(output[index]);
    if (value >= 'a' && value <= 'z')
      output[index] = static_cast<std::byte>(value - 'a' + 'A');
  }
  return ntl::ok(input.size());
}

using inspect_request = ntl_net_kernel_sample::inspect_request;
using inspect_reply = ntl_net_kernel_sample::inspect_reply;

struct inspection_workspace {
  inspect_reply reply{};
  std::array<std::byte, ntl_net_kernel_sample::maximum_wire_size> scratch{};
};

using inspection_workspace_pool =
    ntl::net::kernel::workspace_pool<inspection_workspace,
                                     ntl::pool_tag("wNkN")>;

struct lifetime_workspace {
  lifetime_workspace(KEVENT *completed,
                     std::atomic<bool> *passive) noexcept
      : completed(completed), passive(passive) {}

  ~lifetime_workspace() {
    passive->store(
        ntl::current_irql() == ntl::irql::passive,
        std::memory_order_release);
    KeSetEvent(completed, IO_NO_INCREMENT, FALSE);
  }

  KEVENT *completed = nullptr;
  std::atomic<bool> *passive = nullptr;
};

using lifetime_workspace_pool =
    ntl::net::kernel::workspace_pool<
        lifetime_workspace, ntl::pool_tag("lNkN")>;

ntl::status inspect_workspace_lifetime(inspect_reply &reply) noexcept {
  KEVENT completed{};
  KeInitializeEvent(&completed, NotificationEvent, FALSE);
  std::atomic<bool> passive{false};
  try {
    auto pool = std::make_shared<lifetime_workspace_pool>(
        static_cast<USHORT>(0), std::size_t{1});
    auto acquired = pool->try_acquire(&completed, &passive);
    if (!acquired)
      return acquired.status();
    auto exhausted = pool->try_acquire(&completed, &passive);
    if (exhausted || exhausted.status() != STATUS_QUOTA_EXCEEDED)
      return STATUS_INTERNAL_ERROR;

    auto lease = std::move(*acquired);
    pool->close();
    pool->close();
    auto after_close = pool->try_acquire(&completed, &passive);
    if (after_close || after_close.status() != STATUS_DELETE_PENDING)
      return STATUS_INTERNAL_ERROR;
    pool.reset();
    KIRQL previous = PASSIVE_LEVEL;
    KeRaiseIrql(DISPATCH_LEVEL, &previous);
    lease.reset();
    KeLowerIrql(previous);

    LARGE_INTEGER timeout{};
    timeout.QuadPart = -5LL * 10'000'000LL;
    const NTSTATUS waited = KeWaitForSingleObject(
        &completed, Executive, KernelMode, FALSE, &timeout);
    if (!NT_SUCCESS(waited) || !passive.load(std::memory_order_acquire))
      return NT_SUCCESS(waited) ? STATUS_INVALID_DEVICE_STATE : waited;
    reply.field_count = 2;
    reply.flags =
        ntl_net_kernel_sample::result_flag::workspace_fail_closed |
        ntl_net_kernel_sample::result_flag::workspace_passive_cleanup;
    return ntl::status::ok();
  } catch (const ntl::exception &error) {
    return error.get_status();
  } catch (...) {
    return STATUS_UNHANDLED_EXCEPTION;
  }
}

ntl::status inspect_injection_lifetime(inspect_reply &reply) noexcept {
  auto *domain =
      ntl::net::kernel::borrowed_runtime_passive_cleanup_domain();
  if (!domain)
    return STATUS_DEVICE_NOT_READY;
  auto created = ntl::wfp::transport_injector::try_create(AF_INET);
  if (!created)
    return created.status();
  auto injector = std::move(*created);
  KIRQL previous = PASSIVE_LEVEL;
  KeRaiseIrql(DISPATCH_LEVEL, &previous);
  injector.reset();
  KeLowerIrql(previous);

  const ntl::status flushed = domain->flush_retired();
  if (!flushed.is_ok())
    return flushed;
  reply.field_count = 1;
  reply.flags =
      ntl_net_kernel_sample::result_flag::injection_passive_cleanup;
  return ntl::status::ok();
}

ntl::status inspect_waitable_task_lifetime(inspect_reply &reply) noexcept {
  auto *domain =
      ntl::net::kernel::borrowed_runtime_passive_cleanup_domain();
  if (!domain)
    return STATUS_DEVICE_NOT_READY;
  KEVENT destroyed{};
  KeInitializeEvent(&destroyed, NotificationEvent, FALSE);
  std::atomic<bool> passive{false};
  try {
    auto probe = std::make_shared<task_lifetime_probe>(destroyed, passive);
    auto slot = std::make_shared<task_resume_slot>();
    std::optional<ntl::net::kernel::waitable_status_task> operation{
        finish_task_at_passive(std::move(probe), slot)};
    KIRQL previous = PASSIVE_LEVEL;
    KeRaiseIrql(DISPATCH_LEVEL, &previous);
    // The facade is intentionally released before the coroutine resumes. Its
    // frame and captured state must remain alive without a manual join.
    operation.reset();
    KeLowerIrql(previous);
    if (!slot->continuation)
      return STATUS_INTERNAL_ERROR;
    slot->continuation.resume();
  } catch (const ntl::exception &error) {
    return error.get_status();
  } catch (...) {
    return STATUS_UNHANDLED_EXCEPTION;
  }

  LARGE_INTEGER timeout{};
  timeout.QuadPart = -5LL * 10'000'000LL;
  const NTSTATUS waited = KeWaitForSingleObject(
      &destroyed, Executive, KernelMode, FALSE, &timeout);
  if (!NT_SUCCESS(waited) || !passive.load(std::memory_order_acquire))
    return NT_SUCCESS(waited) ? STATUS_INVALID_DEVICE_STATE : waited;

  const ntl::status flushed = domain->flush_retired();
  if (!flushed.is_ok())
    return flushed;

  reply.field_count = 1;
  reply.flags = ntl_net_kernel_sample::result_flag::task_passive_cleanup;
  return ntl::status::ok();
}

ntl::status inspect_udp_mapping_lifetime(inspect_reply &reply) noexcept {
  using namespace ntl::wfp::transparent_udp_proxy_detail;
  static_assert(sizeof(mapping_table) <= 256,
                "bounded UDP mappings must not consume the kernel stack");
  mapping_table table(4);
  if (!table.valid())
    return STATUS_INSUFFICIENT_RESOURCES;
  endpoint local_a{};
  local_a.address[0] = std::byte{127};
  local_a.address[3] = std::byte{1};
  local_a.port = 41000;
  endpoint remote_a{};
  remote_a.address[0] = std::byte{192};
  remote_a.address[1] = std::byte{0};
  remote_a.address[2] = std::byte{2};
  remote_a.address[3] = std::byte{1};
  remote_a.port = 443;
  endpoint remote_conflict = remote_a;
  remote_conflict.address[3] = std::byte{2};
  endpoint local_same_port_other_address = local_a;
  local_same_port_other_address.address[3] = std::byte{2};
  endpoint local_b = local_a;
  local_b.port = 41001;
  endpoint local_c = local_a;
  local_c.port = 41002;
  constexpr auto compartment = static_cast<COMPARTMENT_ID>(7);
  constexpr std::uint16_t proxy_a = 18443;
  constexpr std::uint16_t proxy_b = 18444;

  KIRQL previous = PASSIVE_LEVEL;
  KeRaiseIrql(DISPATCH_LEVEL, &previous);
  const bool first =
      table.remember(AF_INET, compartment, local_a, remote_a, proxy_a);
  const bool reference =
      table.remember(AF_INET, compartment, local_a, remote_a, proxy_a);
  const bool conflict = table.remember(
      AF_INET, compartment, local_a, remote_conflict, proxy_a);
  const bool distinct_proxy = table.remember(
      AF_INET, compartment, local_a, remote_conflict, proxy_b);
  const bool distinct_address = table.remember(
      AF_INET, compartment, local_same_port_other_address, remote_a, proxy_a);
  const bool second =
      table.remember(AF_INET, compartment, local_b, remote_a, proxy_a);
  const bool exhausted =
      table.remember(AF_INET, compartment, local_c, remote_a, proxy_a);
  table.forget(AF_INET, compartment, local_a, proxy_a);
  mapping found{};
  const bool retained =
      table.find(AF_INET, compartment, local_a, proxy_a, found);
  mapping alternate{};
  const bool alternate_found =
      table.find(AF_INET, compartment, local_a, proxy_b, alternate);
  table.forget(AF_INET, compartment, local_a, proxy_a);
  const bool removed =
      !table.find(AF_INET, compartment, local_a, proxy_a, found);
  const bool reused =
      table.remember(AF_INET, compartment, local_c, remote_a, proxy_a);

  mapping_table reply_table(3);
  endpoint route_selected = local_a;
  route_selected.address[3] = std::byte{99};
  const bool reply_remembered =
      reply_table.remember(AF_INET, compartment, local_a, remote_a, proxy_a);
  mapping route_selected_match{};
  const bool unique_route_selected = reply_table.find_reply(
      AF_INET, compartment, route_selected, proxy_a, route_selected_match);
  const bool reply_ambiguous_entry = reply_table.remember(
      AF_INET, compartment, local_same_port_other_address, remote_conflict,
      proxy_a);
  mapping ambiguous_reply{};
  const bool ambiguous_route_selected = reply_table.find_reply(
      AF_INET, compartment, route_selected, proxy_a, ambiguous_reply);
  mapping exact_reply{};
  const bool exact_route_selected = reply_table.find_reply(
      AF_INET, compartment, local_same_port_other_address, proxy_a,
      exact_reply);

  std::array<std::byte, 28> ipv4{};
  ipv4[0] = std::byte{0x45};
  ipv4[3] = std::byte{28};
  ipv4[9] = std::byte{IPPROTO_UDP};
  ipv4[16] = std::byte{127};
  ipv4[19] = std::byte{2};
  ipv4[20] = static_cast<std::byte>(proxy_a >> 8);
  ipv4[21] = static_cast<std::byte>(proxy_a & 0xff);
  ipv4[22] = static_cast<std::byte>(local_a.port >> 8);
  ipv4[23] = static_cast<std::byte>(local_a.port & 0xff);
  ipv4[25] = std::byte{8};
  const auto parsed_v4 = parse_udp_ip_packet(
      ntl::net::scatter_view::from_contiguous(ipv4), AF_INET);

  std::array<std::byte, 48> ipv6{};
  ipv6[0] = std::byte{0x60};
  ipv6[5] = std::byte{8};
  ipv6[6] = std::byte{IPPROTO_UDP};
  ipv6[39] = std::byte{1};
  ipv6[40] = static_cast<std::byte>(proxy_b >> 8);
  ipv6[41] = static_cast<std::byte>(proxy_b & 0xff);
  ipv6[42] = static_cast<std::byte>(local_b.port >> 8);
  ipv6[43] = static_cast<std::byte>(local_b.port & 0xff);
  ipv6[45] = std::byte{8};
  const auto parsed_v6 = parse_udp_ip_packet(
      ntl::net::scatter_view::from_contiguous(ipv6), AF_INET6);

  auto malformed_v4 = ipv4;
  malformed_v4[3] = std::byte{27};
  const auto rejected_length = parse_udp_ip_packet(
      ntl::net::scatter_view::from_contiguous(malformed_v4), AF_INET);
  KeLowerIrql(previous);

  if (!first || !reference || conflict || !distinct_proxy ||
      !distinct_address || !second || exhausted || !retained ||
      found.references != 1 || !alternate_found ||
      alternate.remote.address != remote_conflict.address || !removed ||
      !reused || table.updates() != 6 || table.misses() != 1 ||
      table.quota_rejections() != 2 || !parsed_v4 || !parsed_v6 ||
      parsed_v4->source_port != proxy_a ||
      parsed_v4->destination_port != local_a.port ||
      parsed_v4->destination_address[0] != std::byte{127} ||
      parsed_v4->destination_address[3] != std::byte{2} ||
      parsed_v6->source_port != proxy_b ||
      parsed_v6->destination_port != local_b.port ||
      parsed_v6->destination_address[15] != std::byte{1} ||
      !reply_remembered || !unique_route_selected ||
      route_selected_match.local.address != local_a.address ||
      !reply_ambiguous_entry || ambiguous_route_selected ||
      !exact_route_selected ||
      exact_reply.local.address != local_same_port_other_address.address ||
      reply_table.misses() != 1 ||
      rejected_length || rejected_length.status() != STATUS_DATA_ERROR)
    return STATUS_DATA_ERROR;
  reply.field_count = 13;
  reply.flags =
      ntl_net_kernel_sample::result_flag::udp_mapping_fail_closed;
  return ntl::status::ok();
}

ntl::status inspect_bounded_wait_set(inspect_reply &reply) noexcept {
  std::array<KEVENT, 5> events{};
  for (auto &event : events)
    KeInitializeEvent(&event, NotificationEvent, FALSE);
  KeSetEvent(&events[2], IO_NO_INCREMENT, FALSE);

  ntl::net::kernel::bounded_wait_set<4> objects;
  for (std::size_t index = 0; index != 4; ++index) {
    if (!objects.try_add_unique(&events[index]))
      return STATUS_INSUFFICIENT_RESOURCES;
  }
  if (!objects.try_add_unique(&events[2]) || objects.size() != 4 ||
      objects.try_add_unique(&events[4]))
    return STATUS_DATA_ERROR;

  LARGE_INTEGER no_wait{};
  const NTSTATUS waited = objects.wait_any(
      Executive, KernelMode, FALSE, &no_wait);
  if (waited != STATUS_WAIT_0 + 2)
    return waited == STATUS_TIMEOUT ? STATUS_DATA_ERROR : waited;
  reply.field_count = objects.size();
  reply.flags = ntl_net_kernel_sample::result_flag::bounded_wait_blocks;
  return ntl::status::ok();
}

ntl::status inspect_grpc(const inspect_request &request,
                         ntl::net::scatter_view wire,
                         inspect_reply &reply) noexcept {
  const auto header = ntl::net::grpc::inspect_header(wire, request.size);
  if (!header)
    return header.status();
  reply.content_size = header->payload_size;
  reply.flags =
      header->compressed ? ntl_net_kernel_sample::result_flag::compressed : 0;
  return ntl::status::ok();
}

ntl::status inspect_websocket(const inspect_request &request,
                              ntl::net::scatter_view wire,
                              inspect_reply &reply) noexcept {
  const auto header = ntl::net::websocket::inspect_header(
      wire, ntl::net::websocket::sender_role::either,
      {.maximum_payload_size = request.wire.size(),
       .allowed_reserved_bits = 0});
  if (!header)
    return header.status();
  const auto decoded =
      ntl::net::websocket::decode_payload_to(wire, *header, reply.transformed);
  if (!decoded)
    return decoded.status();
  reply.content_size = static_cast<std::uint32_t>(*decoded);
  reply.flags = header->masked ? ntl_net_kernel_sample::result_flag::masked : 0;
  return ntl::status::ok();
}

ntl::status inspect_qpack(ntl::net::scatter_view wire,
                          inspection_workspace &workspace) noexcept {
  qpack_observer observer;
  const auto decoded = ntl::net::http3::decode_static_qpack(
      wire, workspace.scratch, observer, workspace.scratch.size());
  if (!decoded)
    return decoded.status();
  workspace.reply.content_size =
      static_cast<std::uint32_t>(decoded->decoded_bytes);
  workspace.reply.field_count = observer.fields;
  return ntl::status::ok();
}

ntl::status inspect_tls(const inspect_request &request,
                        ntl::net::scatter_view wire,
                        inspection_workspace &workspace) noexcept {
  tls_observer observer;
  const auto hello = ntl::net::inspect_tls_client_hello(
      wire, workspace.scratch, observer,
      {.maximum_buffered_ciphertext = request.wire.size(),
       .maximum_client_hello = request.wire.size() - 4,
       .receive_chunk_size = 512,
       .maximum_alpn_protocols = 16});
  if (!hello)
    return hello.status();
  workspace.reply.content_size =
      static_cast<std::uint32_t>(hello->handshake_size);
  workspace.reply.field_count = observer.fields;
  workspace.reply.flags =
      observer.flags | (hello->encrypted_client_hello_extension_present
                            ? ntl_net_kernel_sample::result_flag::ech
                            : 0);
  return ntl::status::ok();
}

ntl::status inspect_transform(ntl::net::scatter_view wire,
                              inspect_reply &reply) noexcept {
  ntl::net::borrowed_transform_pipeline pipeline;
  pipeline.transform(
      {&uppercase_transform, nullptr, ntl::net::execution_path::direct});
  const auto transformed = pipeline.run(
      {.network = {.kind = ntl::net::inspection::content_kind::tcp_message},
       .protocol_features =
           ntl::net::feature_set(ntl::net::network_feature::http1)},
      ntl::net::inspection::content_view(wire), reply.transformed);
  if (!transformed)
    return transformed.status();
  reply.content_size = static_cast<std::uint32_t>(transformed->output_size);
  reply.flags = ntl_net_kernel_sample::result_flag::transformed;
  return ntl::status::ok();
}

ntl::status inspect_executor(ntl::net::kernel::executor &executor,
                             inspect_reply &reply) noexcept {
  if (KeGetCurrentIrql() != PASSIVE_LEVEL)
    return STATUS_INVALID_DEVICE_STATE;
  executor_probe probe;
  const ntl::status posted =
      executor.post_borrowed({&run_executor_probe, &probe});
  if (!posted.is_ok())
    return posted;
  const NTSTATUS waited = KeWaitForSingleObject(&probe.completed, Executive,
                                                KernelMode, FALSE, nullptr);
  if (!NT_SUCCESS(waited))
    return waited;
  reply.field_count = probe.calls.load(std::memory_order_acquire);
  return ntl::status::ok();
}

ntl::status inspect_executor_lifetime(inspect_reply &reply) noexcept {
  if (KeGetCurrentIrql() != PASSIVE_LEVEL)
    return STATUS_INVALID_DEVICE_STATE;
  auto *domain =
      ntl::net::kernel::borrowed_runtime_passive_cleanup_domain();
  if (!domain)
    return STATUS_DEVICE_NOT_READY;
  std::shared_ptr<executor_lifetime_witness> witness;
  std::shared_ptr<executor_lifetime_probe> probe;
  try {
    witness = std::make_shared<executor_lifetime_witness>();
    probe = std::make_shared<executor_lifetime_probe>(witness);
  } catch (...) {
    return STATUS_INSUFFICIENT_RESOURCES;
  }
  std::optional<ntl::net::kernel::executor> owner{
      std::in_place, DelayedWorkQueue, ntl::pool_tag("qExN"),
      ntl::net::kernel::executor_limits{1}};
  if (!owner->creation_status().is_ok())
    return owner->creation_status();
  std::optional<ntl::net::kernel::executor> child{*owner};
  probe->callback_owner.emplace(*child);
  const ntl::status posted = child->post(
      probe, &run_executor_lifetime_probe);
  if (!posted.is_ok())
    return posted;

  // The accepted callback, not declaration order, owns the state now.
  probe.reset();

  LARGE_INTEGER timeout{};
  timeout.QuadPart = -5LL * 10'000'000LL;
  NTSTATUS waited = KeWaitForSingleObject(
      &witness->started, Executive, KernelMode, FALSE, &timeout);
  if (!NT_SUCCESS(waited))
    return waited;

  executor_probe overflow_probe;
  const ntl::status quota_rejected = child->post_borrowed(
      {&run_executor_probe, &overflow_probe});
  if (quota_rejected != STATUS_QUOTA_EXCEEDED)
    return STATUS_INTERNAL_ERROR;

  // Destroy the original facade first, then close twice through a child.
  owner.reset();
  child->close();
  child->close();
  const ntl::status rejected = child->post_borrowed(
      {&run_executor_probe, nullptr});
  if (rejected != STATUS_DELETE_PENDING)
    return STATUS_INTERNAL_ERROR;

  KeSetEvent(&witness->proceed, IO_NO_INCREMENT, FALSE);
  waited = KeWaitForSingleObject(
      &witness->completed, Executive, KernelMode, FALSE, &timeout);
  if (!NT_SUCCESS(waited) ||
      witness->calls.load(std::memory_order_acquire) != 1)
    return NT_SUCCESS(waited) ? STATUS_INTERNAL_ERROR : waited;
  const ntl::status drained = child->drain();
  if (!drained.is_ok())
    return drained;
  if (!witness->owner_destroyed.load(std::memory_order_acquire))
    return STATUS_INTERNAL_ERROR;

  KIRQL previous = PASSIVE_LEVEL;
  KeRaiseIrql(DISPATCH_LEVEL, &previous);
  child.reset();
  KeLowerIrql(previous);
  const ntl::status flushed = domain->flush_retired();
  if (!flushed.is_ok())
    return flushed;

  reply.field_count = 5;
  reply.flags =
      ntl_net_kernel_sample::result_flag::executor_lifetime_safe;
  return ntl::status::ok();
}

ntl::status inspect_http3_origin_pool_lifetime(
    inspect_reply &reply) noexcept {
  if (KeGetCurrentIrql() != PASSIVE_LEVEL)
    return STATUS_INVALID_DEVICE_STATE;
  auto *domain =
      ntl::net::kernel::borrowed_runtime_passive_cleanup_domain();
  if (!domain)
    return STATUS_DEVICE_NOT_READY;

  LARGE_INTEGER timeout{};
  timeout.QuadPart = -5LL * 10'000'000LL;
  const auto wait = [&timeout](KEVENT &event) noexcept -> ntl::status {
    return KeWaitForSingleObject(
        &event, Executive, KernelMode, FALSE, &timeout);
  };
  const auto request = [](std::string path,
                          std::size_t body_size = 0) {
    ntl::net::http3::origin_request value;
    value.server_name = "kernel.example";
    value.method = "GET";
    value.scheme = "https";
    value.authority = "kernel.example";
    value.path = std::move(path);
    value.body.resize(body_size, std::byte{0x41});
    return value;
  };

  // A transport retains the runtime after the original pool facade dies.
  KEVENT delivered{};
  KeInitializeEvent(&delivered, NotificationEvent, FALSE);
  std::atomic<bool> valid_response{false};
  auto owner_first_created =
      ntl::net::kernel::http3_origin_pool::try_create(
          [](ntl::net::http3::origin_request,
             ntl::net::kernel::origin_cancellation_view) {
            ntl::net::http3::origin_response response;
            response.status = 200;
            response.negotiated_protocol = "h3";
            return ntl::ok(std::move(response));
          });
  if (!owner_first_created)
    return owner_first_created.status();
  auto owner_first = std::move(*owner_first_created);
  auto retained_transport = owner_first.make_transport();
  if (!retained_transport)
    return STATUS_INSUFFICIENT_RESOURCES;
  owner_first = {};
  ntl::status submitted = retained_transport->submit(
      1, request("/owner-first"),
      [&delivered, &valid_response](
          ntl::result<ntl::net::http3::origin_response> response) noexcept {
        valid_response.store(
            response && response->status == 200 &&
                response->negotiated_protocol == "h3",
            std::memory_order_release);
        KeSetEvent(&delivered, IO_NO_INCREMENT, FALSE);
      });
  if (!submitted.is_ok())
    return submitted;
  ntl::status waited = wait(delivered);
  if (!waited.is_ok() || !valid_response.load(std::memory_order_acquire))
    return waited.is_ok() ? ntl::status{STATUS_DATA_ERROR} : waited;
  retained_transport.reset();
  ntl::status flushed = domain->flush_retired();
  if (!flushed.is_ok())
    return flushed;

  // Quotas fail closed and close/cancel are safe while a handler is active.
  KEVENT handler_started{};
  KEVENT handler_finished{};
  KeInitializeEvent(&handler_started, NotificationEvent, FALSE);
  KeInitializeEvent(&handler_finished, NotificationEvent, FALSE);
  auto bounded_created =
      ntl::net::kernel::http3_origin_pool::try_create(
          [&handler_started, &handler_finished](
              ntl::net::http3::origin_request,
              ntl::net::kernel::origin_cancellation_view cancellation) {
            KeSetEvent(&handler_started, IO_NO_INCREMENT, FALSE);
            (void)KeWaitForSingleObject(
                cancellation.borrowed_native_event(), Executive,
                KernelMode, FALSE, nullptr);
            KeSetEvent(&handler_finished, IO_NO_INCREMENT, FALSE);
            return ntl::result<ntl::net::http3::origin_response>(
                ntl::unexpected(STATUS_CANCELLED));
          },
          {.maximum_concurrency = 1,
           .maximum_pending_operations = 1,
           .maximum_buffered_request_bytes = 96});
  if (!bounded_created)
    return bounded_created.status();
  auto bounded = std::move(*bounded_created);
  auto bounded_transport = bounded.make_transport();
  if (!bounded_transport)
    return STATUS_INSUFFICIENT_RESOURCES;
  const ntl::status byte_rejected = bounded_transport->submit(
      10, request("/too-large", 128), [](auto) noexcept {});
  if (byte_rejected != STATUS_QUOTA_EXCEEDED)
    return STATUS_INTERNAL_ERROR;
  submitted = bounded_transport->submit(
      11, request("/wait"), [](auto) noexcept {});
  if (!submitted.is_ok())
    return submitted;
  waited = wait(handler_started);
  if (!waited.is_ok())
    return waited;
  const ntl::status operation_rejected = bounded_transport->submit(
      12, request("/overloaded"), [](auto) noexcept {});
  if (operation_rejected != STATUS_QUOTA_EXCEEDED)
    return STATUS_INTERNAL_ERROR;
  bounded_transport->cancel(11);
  waited = wait(handler_finished);
  if (!waited.is_ok())
    return waited;
  for (std::size_t attempt = 0;
       attempt != 5000 &&
       bounded.statistics().pending_operations != 0;
       ++attempt) {
    LARGE_INTEGER delay{};
    delay.QuadPart = -10'000LL;
    (void)KeDelayExecutionThread(KernelMode, FALSE, &delay);
  }
  const auto bounded_statistics = bounded.statistics();
  if (bounded_statistics.pending_operations != 0 ||
      bounded_statistics.completed != 1 ||
      bounded_statistics.canceled != 1 ||
      bounded_statistics.operation_quota_rejections != 1 ||
      bounded_statistics.byte_quota_rejections != 1)
    return STATUS_DATA_ERROR;
  bounded.close();
  bounded.close();
  if (bounded_transport->submit(
          13, request("/after-close"), [](auto) noexcept {}) !=
      STATUS_DELETE_PENDING)
    return STATUS_INTERNAL_ERROR;
  bounded = {};
  KIRQL previous = PASSIVE_LEVEL;
  KeRaiseIrql(DISPATCH_LEVEL, &previous);
  bounded_transport.reset();
  KeLowerIrql(previous);
  flushed = domain->flush_retired();
  if (!flushed.is_ok())
    return flushed;

  // A completion may close the pool that owns it without self-deadlock.
  KEVENT callback_closed{};
  KeInitializeEvent(&callback_closed, NotificationEvent, FALSE);
  auto callback_created =
      ntl::net::kernel::http3_origin_pool::try_create(
          [](ntl::net::http3::origin_request,
             ntl::net::kernel::origin_cancellation_view) {
            ntl::net::http3::origin_response response;
            response.status = 204;
            response.negotiated_protocol = "h3";
            return ntl::ok(std::move(response));
          });
  if (!callback_created)
    return callback_created.status();
  auto callback_owner = std::move(*callback_created);
  auto callback_transport = callback_owner.make_transport();
  if (!callback_transport)
    return STATUS_INSUFFICIENT_RESOURCES;
  auto completion_owner = callback_owner;
  submitted = callback_transport->submit(
      20, request("/callback-close"),
      [owner = std::move(completion_owner), &callback_closed](auto) mutable
          noexcept {
        owner.close();
        owner.close();
        KeSetEvent(&callback_closed, IO_NO_INCREMENT, FALSE);
      });
  if (!submitted.is_ok())
    return submitted;
  waited = wait(callback_closed);
  if (!waited.is_ok())
    return waited;
  if (callback_transport->submit(
          21, request("/after-callback-close"), [](auto) noexcept {}) !=
      STATUS_DELETE_PENDING)
    return STATUS_INTERNAL_ERROR;
  callback_owner = {};
  callback_transport.reset();
  flushed = domain->flush_retired();
  if (!flushed.is_ok())
    return flushed;

  reply.field_count = 11;
  reply.flags = ntl_net_kernel_sample::result_flag::
      http3_origin_pool_lifetime_safe;
  return ntl::status::ok();
}

ntl::status inspect_http1(const inspect_request &request,
                          ntl::net::scatter_view wire,
                          inspect_reply &reply) noexcept {
  const ntl::net::http::http1_message_framer framer(
      ntl::net::http::http1_message_kind::request,
      {.maximum_header_size = request.wire.size(),
       .maximum_body_size = request.wire.size()});
  const auto framed = framer.probe(wire);
  if (framed.state() != ntl::net::framing::probe_state::complete)
    return framed.error();
  reply.content_size = static_cast<std::uint32_t>(framed.frame_size());
  return ntl::status::ok();
}

ntl::status inspect_http2(const inspect_request &request,
                          ntl::net::scatter_view wire,
                          inspect_reply &reply) noexcept {
  const auto frame = ntl::net::http2::frame_view::parse(
      wire, {.maximum_payload_size = request.wire.size()});
  if (!frame)
    return frame.status();
  const auto data = frame->data_payload();
  if (!data)
    return data.status();
  reply.content_size = static_cast<std::uint32_t>(data->size());
  reply.field_count = frame->header().stream_id;
  return ntl::status::ok();
}

ntl::status inspect_http3(const inspect_request &request,
                          ntl::net::scatter_view wire,
                          inspect_reply &reply) noexcept {
  const auto frame = ntl::net::http3::frame_view::parse(
      wire, {.maximum_payload_size = request.wire.size()});
  if (!frame)
    return frame.status();
  if (frame->header().type() != ntl::net::http3::frame_type::data)
    return STATUS_DATA_ERROR;
  reply.content_size = static_cast<std::uint32_t>(frame->payload().size());
  return ntl::status::ok();
}

ntl::status inspect_webtransport(ntl::net::scatter_view wire,
                                 inspect_reply &reply) noexcept {
  const auto stream = ntl::net::http3::webtransport::parse_stream_prefix(
      ntl::net::http3::webtransport::stream_direction::bidirectional, wire);
  if (!stream)
    return stream.status();
  reply.content_size = static_cast<std::uint32_t>(stream->body.size());
  reply.field_count = static_cast<std::uint32_t>(stream->session_id);
  return ntl::status::ok();
}

ntl::status inspect_offload(const inspect_request &request,
                            inspect_reply &reply) noexcept {
  const ntl::net::inspection::context metadata{
      .kind = ntl::net::inspection::content_kind::tcp_message,
      .flow_direction = ntl::net::inspection::direction::outbound,
      .flow_id = 7,
      .source_port = 49152,
      .destination_port = 443,
  };
  const auto offload = ntl::net::offload::make_request(
      ntl::net::offload::operation::inspect_content, 1, metadata,
      ntl::net::feature_set(ntl::net::network_feature::http1), request.size, 0,
      1'000);
  if (!offload)
    return offload.status();
  const ntl::net::runtime_descriptor service{
      .domain = ntl::net::execution_domain::user,
      .path = ntl::net::execution_path::offloaded,
      .features =
          ntl::net::feature_set(ntl::net::network_feature::content_inspection),
      .limits = {.maximum_input_bytes = request.wire.size(),
                 .maximum_output_bytes = reply.transformed.size(),
                 .maximum_buffered_bytes = reply.transformed.size(),
                 .timeout_milliseconds = 1'000,
                 .maximum_in_flight = 1},
  };
  const ntl::status request_status =
      ntl::net::offload::validate(*offload, service);
  const ntl::net::offload::response_header response{
      .kind = offload->kind,
      .request_id = offload->request_id,
      .completion_status = STATUS_SUCCESS,
      .verdict = ntl::net::inspection::verdict::block,
  };
  const ntl::status response_status =
      ntl::net::offload::validate(response, *offload);
  if (!request_status.is_ok() || !response_status.is_ok())
    return request_status.is_ok() ? response_status : request_status;
  reply.content_size = static_cast<std::uint32_t>(sizeof(*offload));
  reply.field_count = static_cast<std::uint32_t>(response.verdict);
  reply.flags = ntl_net_kernel_sample::result_flag::offloaded;
  return ntl::status::ok();
}

ntl::status inspect_codec(const inspect_request &request,
                          std::string_view coding,
                          inspect_reply &reply) noexcept {
  if (KeGetCurrentIrql() != PASSIVE_LEVEL)
    return STATUS_INVALID_DEVICE_STATE;

  ntl::net::inspection::content_encoder_registry encoders;
  ntl::net::inspection::content_decoder_registry decoders;
  try {
    ntl::net::inspection::register_standard_content_encoders(encoders);
    ntl::net::inspection::register_standard_content_decoders(decoders);
  } catch (const std::bad_alloc &) {
    return STATUS_INSUFFICIENT_RESOURCES;
  } catch (...) {
    return STATUS_UNHANDLED_EXCEPTION;
  }

  const auto input =
      std::span<const std::byte>(request.wire).first(request.size);
  auto encoded = ntl::net::inspection::encode_content_encoding(
      encoders, input, coding,
      {.maximum_input_size = request.wire.size(),
       .maximum_encoded_size = reply.transformed.size(),
       .maximum_coding_layers = 1});
  if (!encoded)
    return encoded.status();

  const auto encoded_view = ntl::net::scatter_view::from_contiguous(*encoded);
  auto decoded = ntl::net::inspection::decode_content_encoding(
      decoders, encoded_view, coding,
      {.maximum_encoded_size = reply.transformed.size(),
       .maximum_decoded_size = reply.transformed.size(),
       .maximum_expansion_ratio = 128,
       .maximum_coding_layers = 1});
  if (!decoded)
    return decoded.status();
  if (decoded->size() != input.size() ||
      !std::equal(decoded->begin(), decoded->end(), input.begin()))
    return STATUS_DATA_ERROR;

  std::copy(decoded->begin(), decoded->end(), reply.transformed.begin());
  reply.content_size = static_cast<std::uint32_t>(decoded->size());
  reply.field_count = static_cast<std::uint32_t>(encoded->size());
  reply.flags = ntl_net_kernel_sample::result_flag::compressed |
                ntl_net_kernel_sample::result_flag::codec_round_trip;
  return ntl::status::ok();
}

ntl::status inspect_wsk(const inspect_request &request,
                        ntl::net::kernel::executor &executor,
                        inspect_reply &reply) noexcept {
  if (KeGetCurrentIrql() != PASSIVE_LEVEL ||
      request.size <= sizeof(std::uint16_t))
    return STATUS_INVALID_PARAMETER;

  std::uint16_t port = 0;
  std::memcpy(&port, request.wire.data(), sizeof(port));
  const auto payload = std::span<const std::byte>(request.wire)
                           .subspan(sizeof(port), request.size - sizeof(port));

  auto *const cleanup =
      ntl::net::kernel::borrowed_runtime_passive_cleanup_domain();
  if (!cleanup)
    return STATUS_DEVICE_NOT_READY;
  ntl::net::kernel::wsk_provider provider;
  const ntl::status opened = provider.open({5'000});
  if (!opened.is_ok())
    return opened;

  auto transport = ntl::net::kernel::wsk_tcp_transport::try_connect(
      provider, ntl::net::kernel::ip_endpoint::any_ipv4(),
      ntl::net::kernel::ip_endpoint::ipv4({127, 0, 0, 1}, port),
      {.maximum_write_bytes = request.wire.size(),
       .receive_buffer_bytes = request.wire.size(),
       .connect_timeout = std::chrono::seconds(5)});
  if (!transport)
    return transport.status();

  wsk_provider_close_race_probe race(provider);
  const ntl::status race_posted =
      executor.post_borrowed({&run_wsk_provider_close_race, &race});
  if (!race_posted.is_ok())
    return race_posted;
  LARGE_INTEGER race_timeout{};
  race_timeout.QuadPart = -5LL * 10'000'000LL;
  NTSTATUS race_waited = KeWaitForSingleObject(
      &race.started, Executive, KernelMode, FALSE, &race_timeout);
  if (!NT_SUCCESS(race_waited)) {
    race.stop.store(true, std::memory_order_release);
    (void)KeWaitForSingleObject(
        &race.completed, Executive, KernelMode, FALSE, nullptr);
    return race_waited;
  }
  // Existing transports retain the native registration even when the facade
  // is closed first. The same facade may be observed on another worker while
  // close() atomically rejects new child creation.
  provider.close();
  race.stop.store(true, std::memory_order_release);
  race_waited = KeWaitForSingleObject(
      &race.completed, Executive, KernelMode, FALSE, &race_timeout);
  if (!NT_SUCCESS(race_waited)) {
    (void)KeWaitForSingleObject(
        &race.completed, Executive, KernelMode, FALSE, nullptr);
    return race_waited;
  }
  if (race.observations.load(std::memory_order_acquire) == 0)
    return STATUS_INTERNAL_ERROR;
  provider.close();
  auto rejected = ntl::net::kernel::wsk_tcp_transport::try_connect(
      provider, ntl::net::kernel::ip_endpoint::any_ipv4(),
      ntl::net::kernel::ip_endpoint::ipv4({127, 0, 0, 1}, port));
  if (rejected)
    return STATUS_INTERNAL_ERROR;

  auto stream = ntl::net::io::async_transport_stream::try_create(
      *transport, request.wire.size());
  if (!stream)
    return stream.status();
  std::optional<kernel_status_task> operation{
      wsk_round_trip(*stream, payload, reply)};
  ntl::status completed = operation->wait(std::chrono::seconds(15));
  if (!completed.is_ok()) {
    (*stream)->close();
    completed = operation->wait();
  }
  std::optional<kernel_status_task> drain{
      stop_and_drain_at_owner_boundary(**stream)};
  const ntl::status drained = drain->wait();
  auto owned_stream = std::move(*stream);
  auto owned_transport = std::move(*transport);
  KIRQL previous = PASSIVE_LEVEL;
  KeRaiseIrql(DISPATCH_LEVEL, &previous);
  operation.reset();
  drain.reset();
  owned_stream.reset();
  owned_transport.reset();
  KeLowerIrql(previous);
  const ntl::status flushed = cleanup->flush_retired();
  if (!flushed.is_ok())
    return flushed;
  if (!completed.is_ok() || !drained.is_ok())
    return completed.is_ok() ? drained : completed;

  ntl::net::kernel::wsk_provider relay_provider;
  const ntl::status relay_provider_opened = relay_provider.open({5'000});
  if (!relay_provider_opened.is_ok())
    return relay_provider_opened;
  auto relay_result = ntl::net::kernel::wsk_datagram_relay::try_create(
      std::move(relay_provider), port,
      {.maximum_flows = 4,
       .maximum_pending_datagrams = 8,
       .socket = {.maximum_datagram_bytes = 4096,
                  .maximum_datagrams_per_indication = 4}});
  if (!relay_result)
    return relay_result.status();
  auto relay = std::move(*relay_result);
  if (!relay || relay.local_port() == 0)
    return STATUS_INTERNAL_ERROR;

  wsk_datagram_relay_close_race_probe relay_race(relay);
  const ntl::status relay_race_posted = executor.post_borrowed(
      {&run_wsk_datagram_relay_close_race, &relay_race});
  if (!relay_race_posted.is_ok())
    return relay_race_posted;
  KeClearEvent(&relay_race.completed);
  LARGE_INTEGER relay_race_timeout{};
  relay_race_timeout.QuadPart = -5LL * 10'000'000LL;
  NTSTATUS relay_race_waited = KeWaitForSingleObject(
      &relay_race.started, Executive, KernelMode, FALSE,
      &relay_race_timeout);
  if (!NT_SUCCESS(relay_race_waited)) {
    relay_race.stop.store(true, std::memory_order_release);
    (void)KeWaitForSingleObject(&relay_race.completed, Executive,
                                KernelMode, FALSE, nullptr);
    return relay_race_waited;
  }

  KIRQL relay_previous = PASSIVE_LEVEL;
  KeRaiseIrql(DISPATCH_LEVEL, &relay_previous);
  relay.close();
  relay.close();
  KeLowerIrql(relay_previous);
  relay_race.stop.store(true, std::memory_order_release);
  relay_race_waited = KeWaitForSingleObject(
      &relay_race.completed, Executive, KernelMode, FALSE,
      &relay_race_timeout);
  if (!NT_SUCCESS(relay_race_waited)) {
    (void)KeWaitForSingleObject(&relay_race.completed, Executive,
                                KernelMode, FALSE, nullptr);
    return relay_race_waited;
  }
  if (relay_race.observations.load(std::memory_order_acquire) == 0 ||
      relay || relay.local_port() != 0 ||
      relay.statistics().active_flows != 0)
    return STATUS_INTERNAL_ERROR;
  return cleanup->flush_retired();
}

ntl::status inspect_wsk_listener(const inspect_request &request,
                                 inspect_reply &reply) noexcept {
  if (KeGetCurrentIrql() != PASSIVE_LEVEL ||
      request.size <= sizeof(std::uint16_t))
    return STATUS_INVALID_PARAMETER;
  std::uint16_t port = 0;
  std::memcpy(&port, request.wire.data(), sizeof(port));
  const auto payload = std::span<const std::byte>(request.wire)
                           .subspan(sizeof(port), request.size - sizeof(port));

  ntl::net::kernel::wsk_provider provider;
  const ntl::status opened = provider.open({5'000});
  if (!opened.is_ok())
    return opened;
  auto listener = ntl::net::kernel::wsk_tcp_listener::try_listen(
      provider, ntl::net::kernel::ip_endpoint::ipv4({127, 0, 0, 1}, port),
      {.connection = {.maximum_write_bytes = request.wire.size(),
                      .receive_buffer_bytes = request.wire.size(),
                      .connect_timeout = std::chrono::seconds(5)},
       .accept_timeout = std::chrono::seconds(30)});
  if (!listener)
    return listener.status();
  if ((*listener)->local_endpoint().port() != port)
    return STATUS_DATA_ERROR;
  // The listener owns the captured WSK runtime. Closing the facade first is
  // intentional: child lifetime must not depend on member declaration or
  // destruction order.
  provider.close();
  auto accepted = (*listener)->accept();
  (*listener)->stop();
  if (!accepted)
    return accepted.status();
  auto stream = ntl::net::io::async_transport_stream::try_create(
      *accepted, request.wire.size());
  if (!stream)
    return stream.status();
  auto operation = wsk_listener_round_trip(**stream, payload, reply);
  ntl::status completed = operation.wait(std::chrono::seconds(15));
  if (!completed.is_ok()) {
    (*stream)->close();
    completed = operation.wait();
  }
  auto drain = stop_and_drain_at_owner_boundary(**stream);
  const ntl::status drained = drain.wait();
  return completed.is_ok() ? drained : completed;
}

ntl::status wait_for_read_generation(read_sequence_probe &probe,
                                     std::uint32_t expected) noexcept {
  while (probe.waiting.load(std::memory_order_acquire) < expected) {
    KeClearEvent(&probe.progress);
    if (probe.waiting.load(std::memory_order_acquire) >= expected)
      break;
    LARGE_INTEGER timeout{};
    timeout.QuadPart = -5LL * 1000 * 1000 * 10;
    const NTSTATUS waited = KeWaitForSingleObject(&probe.progress, Executive,
                                                  KernelMode, FALSE, &timeout);
    if (!NT_SUCCESS(waited))
      return waited;
  }
  return ntl::status::ok();
}

ntl::status validate_read_resume_state_machine(inspect_reply &reply) noexcept {
  constexpr std::uint32_t sequence_count = 64;
  auto stream_result = ntl::net::async_byte_stream::try_create(4);
  if (!stream_result)
    return stream_result.status();
  auto stream = std::move(*stream_result);
  read_sequence_probe probe;
  auto sequence = read_reawait_sequence(stream, probe, sequence_count);

  ntl::status producer_status = ntl::status::ok();
  for (std::uint32_t index = 0; index != sequence_count; ++index) {
    producer_status = wait_for_read_generation(probe, index + 1);
    if (!producer_status.is_ok())
      break;
    const std::array<std::byte, 1> byte{static_cast<std::byte>(index)};
    producer_status = stream.append_received_data(
        ntl::net::scatter_view::from_contiguous(byte));
    if (!producer_status.is_ok())
      break;
  }

  ntl::status sequence_status = sequence.wait(std::chrono::seconds(5));
  if (!producer_status.is_ok() || !sequence_status.is_ok()) {
    stream.cancel();
    sequence_status = sequence.wait();
  }
  auto sequence_drain = cancel_and_drain_at_owner_boundary(stream);
  const ntl::status sequence_drained = sequence_drain.wait();
  if (!producer_status.is_ok() || !sequence_status.is_ok() ||
      !sequence_drained.is_ok() || !sequence.result().is_ok() ||
      probe.resumed.load(std::memory_order_acquire) != sequence_count)
    return STATUS_DATA_ERROR;

  std::uint32_t terminal_resumes = 0;
  for (const auto terminal : {byte_stream_race_action::kind::close,
                              byte_stream_race_action::kind::cancel}) {
    auto race_stream_result = ntl::net::async_byte_stream::try_create(4);
    if (!race_stream_result)
      return race_stream_result.status();
    auto race_stream = std::move(*race_stream_result);
    std::atomic<std::uint32_t> resumed{0};
    const NTSTATUS expected = terminal == byte_stream_race_action::kind::close
                                  ? STATUS_END_OF_FILE
                                  : STATUS_CANCELLED;
    auto reader = read_terminal_once(race_stream, expected, resumed);
    byte_stream_race_action append(race_stream,
                                   byte_stream_race_action::kind::append);
    byte_stream_race_action terminate(race_stream, terminal);
    append.queue();
    terminate.queue();
    const ntl::status append_waited = append.wait();
    const ntl::status terminate_waited = terminate.wait();
    ntl::status reader_waited = reader.wait(std::chrono::seconds(5));
    if (!reader_waited.is_ok()) {
      race_stream.cancel();
      reader_waited = reader.wait();
    }
    auto race_drain = cancel_and_drain_at_owner_boundary(race_stream);
    const ntl::status race_drained = race_drain.wait();
    const NTSTATUS append_status = static_cast<NTSTATUS>(append.status());
    if (!append_waited.is_ok() || !terminate_waited.is_ok() ||
        !reader_waited.is_ok() || !race_drained.is_ok() ||
        !reader.result().is_ok() ||
        resumed.load(std::memory_order_acquire) != 1 ||
        (append_status != STATUS_SUCCESS &&
         append_status != STATUS_DELETE_PENDING))
      return STATUS_DATA_ERROR;
    terminal_resumes += resumed.load(std::memory_order_relaxed);
  }

  // An idle drain publishes its continuation and immediately queues a
  // PASSIVE resume worker. Repeat the owner-boundary path so Special Pool and
  // a fast worker catch any await_suspend write after publication.
  for (std::uint32_t iteration = 0; iteration != 256; ++iteration) {
    auto idle_stream_result = ntl::net::async_byte_stream::try_create(1);
    if (!idle_stream_result)
      return idle_stream_result.status();
    auto idle_stream = std::move(*idle_stream_result);
    auto idle_drain = cancel_and_drain_at_owner_boundary(idle_stream);
    const ntl::status idle_status =
        idle_drain.wait(std::chrono::seconds(5));
    if (!idle_status.is_ok() || !idle_drain.result().is_ok())
      return STATUS_DATA_ERROR;
  }

  reply.transformed[0] = static_cast<std::byte>(sequence_count);
  reply.transformed[1] = static_cast<std::byte>(terminal_resumes);
  return ntl::status::ok();
}

ntl::status
validate_transport_read_some(ntl::net::io::async_transport_stream &stream,
                             deferred_transport_backend &backend,
                             inspect_reply &reply) noexcept {
  constexpr std::uint32_t generations = 48;
  read_sequence_probe probe;
  auto reader = transport_read_some_sequence(stream, probe, generations);
  std::size_t offset = 0;
  ntl::status producer_status = ntl::status::ok();
  for (std::uint32_t generation = 0; generation != generations; ++generation) {
    producer_status = wait_for_read_generation(probe, generation + 1);
    if (!producer_status.is_ok())
      break;
    std::array<std::byte, 17> bytes{};
    const std::size_t count = (generation % bytes.size()) + 1;
    for (std::size_t index = 0; index != count; ++index) {
      bytes[index] = static_cast<std::byte>(
          static_cast<unsigned char>((offset + index) & 0xff));
    }
    producer_status =
        backend.deliver(std::span<const std::byte>(bytes).first(count));
    if (!producer_status.is_ok())
      break;
    offset += count;
  }

  ntl::status completed = reader.wait(std::chrono::seconds(5));
  if (!producer_status.is_ok() || !completed.is_ok()) {
    stream.close();
    completed = reader.wait();
  }
  if (!producer_status.is_ok() || !completed.is_ok() ||
      !reader.result().is_ok() ||
      probe.resumed.load(std::memory_order_acquire) != generations)
    return STATUS_DATA_ERROR;
  reply.transformed[2] = static_cast<std::byte>(generations);
  return ntl::status::ok();
}

ntl::status inspect_async_stream_state_machine(inspect_reply &reply) noexcept {
  if (KeGetCurrentIrql() != PASSIVE_LEVEL)
    return STATUS_INVALID_DEVICE_STATE;

  constexpr std::size_t large_payload_size = 96 * 1024;
  auto large_payload = ntl::net::owned_bytes::try_allocate(
      large_payload_size, ntl::net::buffer_limits{large_payload_size},
      ntl::pool_tag("lAtN"));
  if (!large_payload)
    return large_payload.status();

  std::shared_ptr<deferred_transport_backend> backend;
  try {
    backend = std::make_shared<deferred_transport_backend>();
  } catch (...) {
    return STATUS_INSUFFICIENT_RESOURCES;
  }
  auto stream = ntl::net::io::async_transport_stream::try_create(backend, 64);
  if (!stream)
    return stream.status();
  auto operation = deferred_write_sequence(**stream, large_payload->span());
  ntl::status completed = operation.wait(std::chrono::seconds(10));
  if (!completed.is_ok()) {
    (*stream)->close();
    completed = operation.wait();
  }
  ntl::status read_some_status = ntl::status::ok();
  if (completed.is_ok() && operation.result().is_ok())
    read_some_status = validate_transport_read_some(**stream, *backend, reply);
  auto drain = stop_and_drain_at_owner_boundary(**stream);
  const ntl::status drained = drain.wait();
  constexpr std::uint32_t expected_writes =
      3 + static_cast<std::uint32_t>(large_payload_size / (16 * 1024));
  constexpr std::size_t expected_bytes = 9 + 32 + large_payload_size;
  if (!completed.is_ok() || !operation.result().is_ok() ||
      !read_some_status.is_ok() || !drained.is_ok() ||
       backend->writes() != expected_writes ||
       backend->bytes() != expected_bytes || backend->finals() != 1)
    return STATUS_DATA_ERROR;

  const ntl::status reads = validate_read_resume_state_machine(reply);
  if (!reads.is_ok())
    return reads;
  auto joined = validate_bidirectional_join_contract(reply);
  ntl::status join_status = joined.wait(std::chrono::seconds(5));
  if (!join_status.is_ok())
    join_status = joined.wait();
  if (!join_status.is_ok() || !joined.result().is_ok())
    return STATUS_DATA_ERROR;

  std::shared_ptr<deferred_transport_backend> resume_backend;
  try {
    resume_backend = std::make_shared<deferred_transport_backend>();
  } catch (...) {
    return STATUS_INSUFFICIENT_RESOURCES;
  }
  auto resume_stream =
      ntl::net::io::async_transport_stream::try_create(resume_backend, 4);
  if (!resume_stream)
    return resume_stream.status();
  std::atomic<std::uint32_t> resume_drain_completed{0};
  auto resume_drain = read_then_stop_and_drain_from_resume_callback(
      **resume_stream, resume_drain_completed);
  constexpr std::array<std::byte, 1> resume_byte{std::byte{0x7a}};
  const ntl::status delivered = resume_backend->deliver(resume_byte);
  const ntl::status resume_drain_status = resume_drain.wait();
  if (!delivered.is_ok() || !resume_drain_status.is_ok() ||
      !resume_drain.result().is_ok() ||
      resume_drain_completed.load(std::memory_order_acquire) != 1)
    return STATUS_DATA_ERROR;
  reply.transformed[4] = std::byte{1};

  auto *const cleanup =
      ntl::net::kernel::borrowed_runtime_passive_cleanup_domain();
  if (!cleanup)
    return STATUS_DEVICE_NOT_READY;
  {
    deferred_transport_backend managed_backend;
    auto managed = validate_managed_transport_early_return(managed_backend);
    const ntl::status managed_status = managed.wait();
    if (managed_status != STATUS_ACCESS_DENIED ||
        managed.result() != STATUS_ACCESS_DENIED)
      return STATUS_DATA_ERROR;
    reply.transformed[5] = std::byte{1};

    deferred_transport_backend managed_tls_backend;
    auto managed_tls = validate_managed_tls_early_return(managed_tls_backend);
    const ntl::status managed_tls_status = managed_tls.wait();
    if (managed_tls_status != STATUS_ACCESS_DENIED ||
        managed_tls.result() != STATUS_ACCESS_DENIED)
      return STATUS_DATA_ERROR;
  }
  const ntl::status cleanup_flushed = cleanup->flush_retired();
  if (!cleanup_flushed.is_ok())
    return cleanup_flushed;
  reply.transformed[6] = std::byte{1};
  reply.content_size = static_cast<std::uint32_t>(backend->bytes());
  reply.field_count = backend->writes();
  reply.flags = ntl_net_kernel_sample::result_flag::async_stream_serialized;
  return ntl::status::ok();
}

ntl::status inspect_x509(inspect_reply &reply) noexcept {
  if (KeGetCurrentIrql() != PASSIVE_LEVEL)
    return STATUS_INVALID_DEVICE_STATE;
  auto authority = ntl::net::kernel::x509_certificate_authority::try_create(
      "NTL kernel contract CA", {.rsa_bits = 2048,
                                 .maximum_certificate_bytes = 8 * 1024,
                                 .maximum_common_name_bytes = 253,
                                 .validity_days = 7});
  if (!authority)
    return authority.status();
  auto leaf = authority->issue_server("kernel.example");
  if (!leaf)
    return leaf.status();
  auto private_key = leaf->export_full_private_key();
  if (!private_key)
    return private_key.status();
  if (leaf->der().empty() || leaf->der().size() > reply.transformed.size() ||
      private_key->empty())
    return STATUS_BUFFER_OVERFLOW;
  std::copy(leaf->der().begin(), leaf->der().end(), reply.transformed.begin());
  reply.content_size = static_cast<std::uint32_t>(leaf->der().size());
  reply.field_count =
      static_cast<std::uint32_t>(authority->certificate_ref().der().size());
  reply.flags = ntl_net_kernel_sample::result_flag::x509_generated;
  return ntl::status::ok();
}

ntl::status inspect_schannel_client(
    ntl::net::kernel::executor &executor,
    inspect_reply &reply) noexcept {
  if (KeGetCurrentIrql() != PASSIVE_LEVEL)
    return STATUS_INVALID_DEVICE_STATE;
  auto *const cleanup =
      ntl::net::kernel::borrowed_runtime_passive_cleanup_domain();
  if (!cleanup)
    return STATUS_DEVICE_NOT_READY;
  constexpr std::array<std::byte, 7> leaf_der{
      std::byte{0x30}, std::byte{0x05}, std::byte{0x02}, std::byte{0x01},
      std::byte{0x01}, std::byte{0x05}, std::byte{0x00}};
  std::array<std::byte, sizeof(ULONG) + leaf_der.size()> encoded_chain{};
  const ULONG leaf_size = static_cast<ULONG>(leaf_der.size());
  std::memcpy(encoded_chain.data(), &leaf_size, sizeof(leaf_size));
  std::copy(leaf_der.begin(), leaf_der.end(),
            encoded_chain.begin() + sizeof(ULONG));
  auto chain =
      ntl::net::kernel::schannel_peer_certificate_chain::try_copy_encoded(
          encoded_chain, 1,
          {.maximum_peer_certificate_chain = encoded_chain.size(),
           .maximum_peer_certificates = 1});
  if (!chain || chain->size() != 1)
    return chain ? ntl::status{STATUS_DATA_ERROR} : chain.status();
  ntl::net::kernel::detail::native_secpkg_context_certificates native_chain{
      .certificate_count = 1,
      .certificate_chain_size =
          static_cast<ULONG>(encoded_chain.size()),
      .certificate_chain =
          reinterpret_cast<UCHAR *>(encoded_chain.data())};
  auto copied_native = ntl::net::kernel::schannel_peer_certificate_chain::
      try_copy_native_schannel_chain(
          &native_chain,
          {.maximum_peer_certificate_chain = encoded_chain.size(),
           .maximum_peer_certificates = 1});
  if (!copied_native || copied_native->size() != 1)
    return copied_native ? ntl::status{STATUS_DATA_ERROR}
                         : copied_native.status();
  auto pin =
      ntl::net::kernel::schannel_exact_leaf_certificate_policy::try_create(
          leaf_der);
  if (!pin || !pin->verify(*chain, L"kernel.example").is_ok() ||
      !pin->verify(*copied_native, L"kernel.example").is_ok())
    return pin ? ntl::status{STATUS_ACCESS_DENIED} : pin.status();
  constexpr std::array<std::byte, 1> different{std::byte{0x31}};
  auto rejected_pin =
      ntl::net::kernel::schannel_exact_leaf_certificate_policy::try_create(
          different);
  if (!rejected_pin ||
      rejected_pin->verify(*chain, L"kernel.example").is_ok())
    return STATUS_DATA_ERROR;
  auto malformed = encoded_chain;
  const ULONG oversized = static_cast<ULONG>(leaf_der.size() + 1);
  std::memcpy(malformed.data(), &oversized, sizeof(oversized));
  if (ntl::net::kernel::schannel_peer_certificate_chain::try_copy_encoded(
          malformed, 1,
          {.maximum_peer_certificate_chain = malformed.size(),
           .maximum_peer_certificates = 1}))
    return STATUS_DATA_ERROR;

  ntl::net::kernel::schannel schannel;
  auto credentials = schannel.try_client(
      {.manual_peer_validation = true,
       .use_default_client_certificate = false});
  if (!credentials)
    return credentials.status();
  ntl::net::kernel::schannel_context context(
      {.maximum_handshake_token = reply.transformed.size(),
       .maximum_ciphertext = reply.transformed.size(),
       .maximum_plaintext = reply.transformed.size()});
  const ntl::status target = context.set_client_target(L"kernel.example");
  if (!target.is_ok())
    return target;
  constexpr std::array<std::string_view, 2> protocols{"h2", "http/1.1"};
  const ntl::status alpn = context.set_application_protocols(protocols);
  if (!alpn.is_ok())
    return alpn;
  auto hello = context.handshake(*credentials);
  if (!hello)
    return hello.status();
  if (hello->complete() || !hello->needs_input() ||
      hello->output_token.empty() ||
      hello->output_token.size() > reply.transformed.size())
    return STATUS_DATA_ERROR;
  std::copy(hello->output_token.span().begin(),
            hello->output_token.span().end(), reply.transformed.begin());
  reply.content_size = static_cast<std::uint32_t>(hello->output_token.size());
  reply.field_count =
      static_cast<std::uint32_t>(static_cast<ULONG>(hello->security_status));
  {
    ntl::net::kernel::schannel cleanup_owner;
    auto deferred = cleanup_owner.try_client();
    if (!deferred)
      return deferred.status();
    ntl::net::kernel::schannel_credentials retained =
        std::move(*deferred);
    ntl::net::kernel::schannel_credentials released = retained;
    KSPIN_LOCK release_lock{};
    KeInitializeSpinLock(&release_lock);
    KIRQL old_irql = PASSIVE_LEVEL;
    KeAcquireSpinLock(&release_lock, &old_irql);
    released = {};
    KeReleaseSpinLock(&release_lock, old_irql);
    if (!retained || cleanup_owner.live_credentials() != 1)
      return STATUS_DATA_ERROR;
    retained = {};
    const ntl::status drained = cleanup_owner.close();
    if (!drained.is_ok() || cleanup_owner.live_credentials() != 0)
      return drained.is_ok() ? ntl::status{STATUS_DATA_ERROR} : drained;
  }
  {
    ntl::net::kernel::schannel owner;
    auto created = owner.try_client();
    if (!created)
      return created.status();
    ntl::net::kernel::schannel_credentials late = std::move(*created);
    const ntl::status closed = owner.close();
    if (!closed.is_ok() || late)
      return closed.is_ok() ? ntl::status{STATUS_DATA_ERROR} : closed;
    KSPIN_LOCK release_lock{};
    KeInitializeSpinLock(&release_lock);
    KIRQL old_irql = PASSIVE_LEVEL;
    KeAcquireSpinLock(&release_lock, &old_irql);
    late = {};
    KeReleaseSpinLock(&release_lock, old_irql);
  }
  {
    ntl::net::kernel::schannel owner;
    auto created = owner.try_client();
    if (!created)
      return created.status();
    ntl::net::kernel::schannel_credentials shared = std::move(*created);
    schannel_close_race_probe race(owner, shared);
    const ntl::status posted =
        executor.post_borrowed({&run_schannel_close_race, &race});
    if (!posted.is_ok())
      return posted;
    LARGE_INTEGER timeout{};
    timeout.QuadPart = -5LL * 10'000'000LL;
    NTSTATUS waited = KeWaitForSingleObject(
        &race.started, Executive, KernelMode, FALSE, &timeout);
    if (!NT_SUCCESS(waited)) {
      race.stop.store(true, std::memory_order_release);
      (void)KeWaitForSingleObject(
          &race.completed, Executive, KernelMode, FALSE, nullptr);
      return waited;
    }
    const ntl::status closed = owner.close();
    KIRQL previous = PASSIVE_LEVEL;
    KeRaiseIrql(DISPATCH_LEVEL, &previous);
    shared = {};
    KeLowerIrql(previous);
    race.stop.store(true, std::memory_order_release);
    waited = KeWaitForSingleObject(
        &race.completed, Executive, KernelMode, FALSE, &timeout);
    if (!NT_SUCCESS(waited)) {
      (void)KeWaitForSingleObject(
          &race.completed, Executive, KernelMode, FALSE, nullptr);
      return waited;
    }
    if (!closed.is_ok() ||
        race.observations.load(std::memory_order_acquire) == 0)
      return closed.is_ok() ? ntl::status{STATUS_INTERNAL_ERROR} : closed;
  }
  {
    ntl::net::kernel::schannel owner;
    auto created = owner.try_client();
    if (!created)
      return created.status();
    ntl::net::kernel::schannel_credentials credential =
        std::move(*created);
    schannel_cleanup_reentry_probe reentry(owner);
    const ntl::status registered = cleanup->register_item(reentry.item);
    if (!registered.is_ok())
      return registered;
    cleanup->retire_deferred(reentry.item);
    LARGE_INTEGER timeout{};
    timeout.QuadPart = -5LL * 10'000'000LL;
    NTSTATUS waited = KeWaitForSingleObject(
        &reentry.completed, Executive, KernelMode, FALSE, &timeout);
    if (!NT_SUCCESS(waited)) {
      (void)KeWaitForSingleObject(
          &reentry.completed, Executive, KernelMode, FALSE, nullptr);
      return waited;
    }
    if (!reentry.result.is_ok())
      return reentry.result;
    credential = {};
    const ntl::status flushed = cleanup->flush_retired();
    if (!flushed.is_ok() || owner.live_credentials() != 0)
      return flushed.is_ok() ? ntl::status{STATUS_INTERNAL_ERROR} : flushed;
  }
  reply.flags =
      ntl_net_kernel_sample::result_flag::schannel_client_hello |
      ntl_net_kernel_sample::result_flag::credential_passive_cleanup;
  return ntl::status::ok();
}

ntl::status inspect_wsk_tls(const inspect_request &request,
                            ntl::net::kernel::executor &executor,
                            inspect_reply &reply) noexcept {
  if (KeGetCurrentIrql() != PASSIVE_LEVEL ||
      request.size <= sizeof(std::uint16_t))
    return STATUS_INVALID_PARAMETER;
  std::uint16_t port = 0;
  std::memcpy(&port, request.wire.data(), sizeof(port));
  const auto payload = std::span<const std::byte>(request.wire)
                           .subspan(sizeof(port), request.size - sizeof(port));
  auto *const cleanup =
      ntl::net::kernel::borrowed_runtime_passive_cleanup_domain();
  if (!cleanup)
    return STATUS_DEVICE_NOT_READY;
  ntl::net::kernel::wsk_provider provider;
  const ntl::status opened = provider.open({5'000});
  if (!opened.is_ok())
    return opened;
  auto native = ntl::net::kernel::wsk_tcp_transport::try_connect(
      provider, ntl::net::kernel::ip_endpoint::any_ipv4(),
      ntl::net::kernel::ip_endpoint::ipv4({127, 0, 0, 1}, port),
      {.maximum_write_bytes = 256 * 1024,
       .receive_buffer_bytes = 64 * 1024,
       .connect_timeout = std::chrono::seconds(5)});
  if (!native)
    return native.status();
  provider.close();
  auto transport =
      ntl::net::io::async_transport_stream::try_create(*native, 256 * 1024);
  if (!transport)
    return transport.status();
  std::optional<kernel_status_task> operation{
      wsk_tls_round_trip(*transport, payload, executor, reply)};
  ntl::status completed = operation->wait(std::chrono::seconds(20));
  if (!completed.is_ok()) {
    (*transport)->close();
    completed = operation->wait();
  }
  std::optional<kernel_status_task> drain{
      stop_and_drain_at_owner_boundary(**transport)};
  const ntl::status drained = drain->wait();
  auto owned_stream = std::move(*transport);
  auto owned_native = std::move(*native);
  KIRQL previous = PASSIVE_LEVEL;
  KeRaiseIrql(DISPATCH_LEVEL, &previous);
  operation.reset();
  drain.reset();
  owned_stream.reset();
  owned_native.reset();
  KeLowerIrql(previous);
  const ntl::status flushed = cleanup->flush_retired();
  if (!flushed.is_ok())
    return flushed;
  return completed.is_ok() ? drained : completed;
}

ntl::status inspect_msquic_nmr(
    ntl::net::kernel::executor &executor,
    inspect_reply &reply) noexcept {
  if (KeGetCurrentIrql() != PASSIVE_LEVEL)
    return STATUS_INVALID_DEVICE_STATE;
  auto *const cleanup =
      ntl::net::kernel::borrowed_runtime_passive_cleanup_domain();
  if (!cleanup)
    return STATUS_DEVICE_NOT_READY;
  // A distinct module ID keeps the contract driver isolated from every
  // product driver that may bind to the same provider in the VM.
  const GUID module_id{0x5f3624fb,
                       0xa641,
                       0x4fac,
                       {0x95, 0xef, 0x23, 0x0f, 0x16, 0x5c, 0xe9, 0xc7}};
  auto provider = ntl::net::kernel::msquic_provider::try_open(
      {.module_id = module_id, .registration_timeout_milliseconds = 5'000});
  if (!provider)
    return provider.status();
  auto registration =
      ntl::net::kernel::msquic_registration::try_open(*provider);
  if (!registration)
    return registration.status();
  constexpr std::array<std::string_view, 1> protocols{"h3"};
  auto configuration = ntl::net::kernel::msquic_configuration::try_open(
      *registration, protocols);
  if (!configuration)
    return configuration.status();
  const ntl::status credentials =
      configuration->load_client_credentials(true, true);
  if (!credentials.is_ok())
    return credentials;
  std::shared_ptr<rejecting_msquic_listener_sink> listener_sink;
  try {
    listener_sink = std::make_shared<rejecting_msquic_listener_sink>();
  } catch (...) {
    return STATUS_INSUFFICIENT_RESOURCES;
  }
  auto listener = ntl::net::kernel::msquic_listener::try_listen(
      *registration, std::move(listener_sink), protocols);
  if (!listener)
    return listener.status();
  // Regression: native handles must be captured before the shared owner is
  // moved into the context. A single call expression previously allowed the
  // optimizer to empty the local shared_ptr before evaluating its accessors.
  auto connection_context = configuration->make_connection_context();
  if (!connection_context)
    return STATUS_INTERNAL_ERROR;
  msquic_facade_close_race_probe race(
      *provider, *registration, *configuration, *listener);
  const ntl::status race_posted =
      executor.post_borrowed({&run_msquic_facade_close_race, &race});
  if (!race_posted.is_ok())
    return race_posted;
  LARGE_INTEGER race_timeout{};
  race_timeout.QuadPart = -5LL * 10'000'000LL;
  NTSTATUS race_waited = KeWaitForSingleObject(
      &race.started, Executive, KernelMode, FALSE, &race_timeout);
  if (!NT_SUCCESS(race_waited)) {
    race.stop.store(true, std::memory_order_release);
    (void)KeWaitForSingleObject(
        &race.completed, Executive, KernelMode, FALSE, nullptr);
    return race_waited;
  }
  // The configuration retains registration/provider native state. Closing
  // the parent facades first must remain destruction-safe and idempotent.
  provider->close();
  provider->close();
  auto rejected_registration =
      ntl::net::kernel::msquic_registration::try_open(*provider);
  const bool accepted_after_close =
      static_cast<bool>(rejected_registration);
  registration->close();
  registration->close();
  KIRQL previous = PASSIVE_LEVEL;
  KeRaiseIrql(DISPATCH_LEVEL, &previous);
  listener->close();
  listener->close();
  configuration->close();
  configuration->close();
  KeLowerIrql(previous);
  race.stop.store(true, std::memory_order_release);
  race_waited = KeWaitForSingleObject(
      &race.completed, Executive, KernelMode, FALSE, &race_timeout);
  if (!NT_SUCCESS(race_waited)) {
    (void)KeWaitForSingleObject(
        &race.completed, Executive, KernelMode, FALSE, nullptr);
    return race_waited;
  }
  if (race.observations.load(std::memory_order_acquire) == 0)
    return STATUS_INTERNAL_ERROR;
  if (accepted_after_close)
    return STATUS_INTERNAL_ERROR;

  // Drop the final retained configuration owner at DISPATCH. Native closure
  // must still run through the joined PASSIVE cleanup domain.
  KeRaiseIrql(DISPATCH_LEVEL, &previous);
  connection_context = {};
  KeLowerIrql(previous);
  const ntl::status flushed = cleanup->flush_retired();
  if (!flushed.is_ok())
    return flushed;
  reply.content_size = QUIC_API_VERSION_2;
  reply.field_count = static_cast<std::uint32_t>(protocols.size());
  reply.flags = ntl_net_kernel_sample::result_flag::msquic_nmr_bound;
  return ntl::status::ok();
}

ntl::status inspect_dynamic_qpack(inspect_reply &reply) noexcept {
  if (KeGetCurrentIrql() != PASSIVE_LEVEL)
    return STATUS_INVALID_DEVICE_STATE;
  const ntl::net::http3::dynamic_qpack_limits limits{
      .maximum_table_capacity = 256,
      .maximum_blocked_streams = 2,
      .maximum_encoder_stream_buffer = 256,
      .maximum_literal_size = 4096};
  ntl::net::http3::qpack_decoder_adapter<
      ntl::net::http3::bounded_dynamic_qpack_decoder>
      decoder(limits);
  ntl::net::http3::borrowed_connection_inspector inspector(
      decoder,
      {.maximum_concurrent_request_streams = 4,
       .maximum_buffered_bytes_per_stream = 4096,
       .frames = {4096}},
      4096);
  dynamic_qpack_observer observer;
  // HEADERS frame whose block references dynamic entry zero.  It must remain
  // buffered while Required Insert Count=1 is unavailable.
  constexpr std::array<std::byte, 5> headers{std::byte{0x01}, std::byte{0x03},
                                             std::byte{0x02}, std::byte{0x00},
                                             std::byte{0x80}};
  const auto headers_view = ntl::net::scatter_view::from_contiguous(headers);
  const ntl::status blocked =
      inspector.consume_request_stream(4, headers_view, true, observer);
  if (blocked != STATUS_RETRY || observer.fields != 0)
    return STATUS_DATA_ERROR;

  // Capacity=64, Insert With Literal Name x=y.  Split delivery proves that
  // fragmented QPACK encoder streams are retained safely in kernel mode.
  constexpr std::array<std::byte, 6> encoder{std::byte{0x3f}, std::byte{0x21},
                                             std::byte{0x41}, std::byte{0x78},
                                             std::byte{0x01}, std::byte{0x79}};
  auto first = ntl::net::scatter_view::from_contiguous(
      std::span<const std::byte>(encoder).first(3));
  auto second = ntl::net::scatter_view::from_contiguous(
      std::span<const std::byte>(encoder).subspan(3));
  const ntl::status consumed_first =
      inspector.consume_qpack_encoder_stream(first);
  const ntl::status consumed_second =
      inspector.consume_qpack_encoder_stream(second);
  if (!consumed_first.is_ok() || !consumed_second.is_ok())
    return consumed_first.is_ok() ? consumed_second : consumed_first;
  const ntl::status resumed = inspector.resume_request_stream(4, observer);
  if (!resumed.is_ok() || observer.fields != 1)
    return resumed.is_ok() ? ntl::status{STATUS_DATA_ERROR} : resumed;
  auto decoder_wire = inspector.take_qpack_decoder_stream();
  if (!decoder_wire || decoder_wire->size() != 2)
    return decoder_wire ? ntl::status{STATUS_DATA_ERROR}
                        : decoder_wire.status();
  reply.content_size = static_cast<std::uint32_t>(decoder_wire->size());
  reply.field_count = observer.fields;
  reply.flags = ntl_net_kernel_sample::result_flag::qpack_resumed;
  return ntl::status::ok();
}

ntl::status inspect_webtransport_backend(inspect_reply &reply) noexcept {
  if (KeGetCurrentIrql() != PASSIVE_LEVEL)
    return STATUS_INVALID_DEVICE_STATE;
  auto backend_owner =
      std::make_shared<webtransport_contract_backend>();
  auto &backend = *backend_owner;
  ntl::net::http3::webtransport::backend_session session(
      backend_owner, {.maximum_bidirectional_streams = 4,
                .maximum_unidirectional_streams = 4,
                .maximum_stream_data = 4096,
                .maximum_datagram_payload = 1024,
                .maximum_datagrams = 4});
  session.set_negotiated_transport(
      {.quic_datagrams = true, .reliable_reset_at = true});
  ntl::status status = session.send_local_settings(false);
  if (status.is_ok())
    status = session.open_client({.authority = "kernel.example",
                                  .path = "/inspection",
                                  .origin = "https://kernel.example",
                                  .browser_client = true});
  if (status.is_ok())
    status = session.accept_client_response(session.session_id(), 200);
  if (!status.is_ok())
    return status;
  auto stream = session.open_bidirectional_stream();
  if (!stream)
    return stream.status();
  constexpr std::array<std::byte, 3> payload{std::byte{'n'}, std::byte{'t'},
                                             std::byte{'l'}};
  status = session.write(*stream, payload, false);
  if (status.is_ok())
    status = session.send_datagram(payload);
  if (status.is_ok())
    status = session.reset(*stream, 7);
  if (status.is_ok())
    status = session.finish();
  if (!status.is_ok() || backend.datagram_bytes != payload.size() ||
      backend.resets != 1 || backend.writes < 4)
    return status.is_ok() ? ntl::status{STATUS_DATA_ERROR} : status;
  backend.stop();
  status = backend.drain();
  if (!status.is_ok())
    return status;
  reply.content_size = static_cast<std::uint32_t>(backend.bytes);
  reply.field_count = backend.writes;
  reply.flags = ntl_net_kernel_sample::result_flag::webtransport_session;
  return ntl::status::ok();
}

ntl::net::kernel::waitable_status_task wait_for_http2_send_credit(
    ntl::net::http2::send_window &window,
    std::atomic<std::size_t> &remaining_stack) noexcept {
  try {
    co_await window.reserve(1, 1);
    remaining_stack.store(IoGetRemainingStackSize(),
                          std::memory_order_release);
    co_return ntl::status::ok();
  } catch (const ntl::exception &error) {
    co_return ntl::status{error.get_status()};
  } catch (...) {
    co_return ntl::status{STATUS_UNHANDLED_EXCEPTION};
  }
}

ntl::status inspect_http_transform(inspect_reply &reply) noexcept {
  if (KeGetCurrentIrql() != PASSIVE_LEVEL)
    return STATUS_INVALID_DEVICE_STATE;
  try {
    ntl::net::http::transform_pipeline pipeline;
    pipeline.requests().transform([](ntl::net::http::request_message &message) {
      message.headers.set("x-ntl-runtime", "kernel");
      return ntl::net::http::rewrite_result::headers_changed();
    });
    pipeline.responses().html().transform(
        [](const ntl::net::http::request_message &,
           ntl::net::http::response_message &message) {
          constexpr std::string_view marker = "<b>kernel-ntl</b>";
          const std::string_view body(
              reinterpret_cast<const char *>(message.body.data()),
              message.body.size());
          const std::size_t tail = body.find("</body>");
          if (tail == std::string_view::npos)
            return ntl::net::http::rewrite_result::block();
          std::string rewritten(body);
          rewritten.insert(tail, marker);
          return ntl::net::http::rewrite_result::replace_text(
              std::move(rewritten));
        });

    constexpr std::array protocols{ntl::net::http::protocol::http1,
                                   ntl::net::http::protocol::http2,
                                   ntl::net::http::protocol::http3};
    ntl::net::http::message_body last_body;
    for (const auto protocol : protocols) {
      ntl::net::http::request_message request;
      request.wire_protocol = protocol;
      request.method = "GET";
      request.scheme = "https";
      request.authority = "kernel.example";
      request.path = "/";
      const auto request_outcome = pipeline.apply(request);
      if (request_outcome.action != ntl::net::http::rewrite_action::forward ||
          request.headers.first("x-ntl-runtime") != "kernel")
        return STATUS_DATA_ERROR;

      ntl::net::http::response_message response;
      response.wire_protocol = protocol;
      response.status = 200;
      response.headers.append("content-type", "text/html; charset=utf-8");
      constexpr std::string_view html = "<html><body>ok</body></html>";
      response.body.assign(
          reinterpret_cast<const std::byte *>(html.data()),
          reinterpret_cast<const std::byte *>(html.data() + html.size()));
      response.headers.append("content-length",
                              std::to_string(response.body.size()));
      const auto response_outcome = pipeline.apply(request, response);
      if (response_outcome.action != ntl::net::http::rewrite_action::forward ||
          !response_outcome.body_modified ||
          response.headers.first("content-length") !=
              std::to_string(response.body.size()))
        return STATUS_DATA_ERROR;
      last_body = std::move(response.body);
    }

    // Exercise the kernel-only continuation boundary used by the real
    // HTTP/2 proxy. A frame-only parser contract would not cover the
    // header-block assembler/HPACK/policy call chain that runs on a transport
    // coroutine resume.
    ntl::net::http::request_message http2_request;
    http2_request.wire_protocol = ntl::net::http::protocol::http2;
    http2_request.method = "GET";
    http2_request.scheme = "https";
    http2_request.authority = "kernel.example";
    http2_request.path = "/stack-boundary";
    http2_request.headers.set("content-length", "0");
    auto encoded_http2 = ntl::net::http2::encode_request_frames(
        1, http2_request, {}, 16);
    if (!encoded_http2)
      return encoded_http2.status();
    auto exchanges = std::make_shared<ntl::net::http2::exchange_store>();
    ntl::net::http2::connection_transformer http2_transformer(
        ntl::net::http2::connection_direction::requests,
        std::move(exchanges), pipeline,
        ntl::net::inspection::content_decoder_registry{},
        ntl::net::inspection::content_encoder_registry{});
    bool http2_complete = false;
    for (const auto &encoded : *encoded_http2) {
      auto frame = ntl::net::http2::frame_view::parse(
          ntl::net::scatter_view::from_contiguous(encoded.wire),
          {ntl::net::http2::absolute_maximum_frame_size, false});
      if (!frame)
        return frame.status();
      auto transformed = http2_transformer.consume(*frame);
      if (!transformed)
        return transformed.status();
      if (transformed->message_complete) {
        http2_complete = transformed->request &&
                         transformed->request->headers.first(
                             "x-ntl-runtime") == "kernel";
      }
    }
    if (!http2_complete)
      return STATUS_DATA_ERROR;

    // Exercise the real cross-relay control path. Connection and stream
    // WINDOW_UPDATE frames release a suspended response writer from inside
    // observe_control(); the continuation must run on a fresh PASSIVE worker
    // stack rather than nesting both relays on the current worker stack.
    auto connection_pipeline =
        std::make_shared<ntl::net::http::transform_pipeline>();
    auto connection_decoders = std::make_shared<
        ntl::net::inspection::content_decoder_registry>();
    auto connection_encoders = std::make_shared<
        ntl::net::inspection::content_encoder_registry>();
    ntl::net::http2::proxy_connection_limits connection_limits{};
    connection_limits.initial_connection_window = 0;
    connection_limits.initial_stream_window = 0;
    connection_limits.require_first_settings = false;
    ntl::net::http2::proxy_connection connection(
        std::move(connection_pipeline), std::move(connection_decoders),
        std::move(connection_encoders), connection_limits);
    const ntl::status preface = connection.accept_client_preface(
        ntl::net::http2::client_connection_preface);
    if (!preface.is_ok())
      return preface;

    std::atomic<std::size_t> resumed_stack{0};
    auto credit_waiter = wait_for_http2_send_credit(
        connection.destination_window_ref(
            ntl::net::http2::connection_direction::responses),
        resumed_stack);
    const auto consume_credit = [&](std::uint32_t stream_id) -> ntl::status {
      auto encoded = ntl::net::http2::encode_window_update(stream_id, 1);
      if (!encoded)
        return encoded.status();
      auto frame = ntl::net::http2::frame_view::parse(
          ntl::net::scatter_view::from_contiguous(encoded->wire),
          {ntl::net::http2::absolute_maximum_frame_size, false});
      if (!frame)
        return frame.status();
      auto consumed = connection.consume(
          ntl::net::http2::connection_direction::requests, *frame);
      return consumed ? ntl::status::ok() : consumed.status();
    };
    const ntl::status connection_credit = consume_credit(0);
    if (!connection_credit.is_ok())
      return connection_credit;
    const ntl::status stream_credit = consume_credit(1);
    if (!stream_credit.is_ok())
      return stream_credit;
    const ntl::status credit_status =
        credit_waiter.wait(std::chrono::seconds(1));
    constexpr std::size_t minimum_deferred_resume_stack = 16 * 1024;
    if (!credit_status.is_ok() ||
        resumed_stack.load(std::memory_order_acquire) <
            minimum_deferred_resume_stack)
      return credit_status.is_ok() ? ntl::status{STATUS_STACK_OVERFLOW}
                                   : credit_status;

    if (last_body.empty() || last_body.size() > reply.transformed.size())
      return STATUS_BUFFER_OVERFLOW;
    std::copy(last_body.begin(), last_body.end(), reply.transformed.begin());
    reply.content_size = static_cast<std::uint32_t>(last_body.size());
    reply.field_count = static_cast<std::uint32_t>(protocols.size());
    reply.flags = ntl_net_kernel_sample::result_flag::http_all_versions |
                  ntl_net_kernel_sample::result_flag::transformed |
                  ntl_net_kernel_sample::result_flag::http2_resume_stack_safe;
    return ntl::status::ok();
  } catch (const std::bad_alloc &) {
    return STATUS_INSUFFICIENT_RESOURCES;
  } catch (...) {
    return STATUS_UNHANDLED_EXCEPTION;
  }
}

ntl::status inspect(const inspect_request &request,
                    ntl::net::kernel::executor &executor,
                    inspection_workspace &workspace) noexcept {
  if (request.size == 0 || request.size > request.wire.size())
    return STATUS_INVALID_PARAMETER;

  const auto contiguous =
      std::span<const std::byte>(request.wire).first(request.size);
  const auto wire = ntl::net::scatter_view::from_contiguous(contiguous);

  switch (request.kind) {
  case ntl_net_kernel_sample::protocol::grpc:
    return inspect_grpc(request, wire, workspace.reply);
  case ntl_net_kernel_sample::protocol::websocket:
    return inspect_websocket(request, wire, workspace.reply);
  case ntl_net_kernel_sample::protocol::qpack:
    return inspect_qpack(wire, workspace);
  case ntl_net_kernel_sample::protocol::tls_client_hello:
    return inspect_tls(request, wire, workspace);
  case ntl_net_kernel_sample::protocol::transform:
    return inspect_transform(wire, workspace.reply);
  case ntl_net_kernel_sample::protocol::executor:
    return inspect_executor(executor, workspace.reply);
  case ntl_net_kernel_sample::protocol::http1:
    return inspect_http1(request, wire, workspace.reply);
  case ntl_net_kernel_sample::protocol::http2:
    return inspect_http2(request, wire, workspace.reply);
  case ntl_net_kernel_sample::protocol::http3:
    return inspect_http3(request, wire, workspace.reply);
  case ntl_net_kernel_sample::protocol::webtransport:
    return inspect_webtransport(wire, workspace.reply);
  case ntl_net_kernel_sample::protocol::offload_contract:
    return inspect_offload(request, workspace.reply);
  case ntl_net_kernel_sample::protocol::codec_gzip:
    return inspect_codec(request, "gzip", workspace.reply);
  case ntl_net_kernel_sample::protocol::codec_brotli:
    return inspect_codec(request, "br", workspace.reply);
  case ntl_net_kernel_sample::protocol::wsk_tcp:
    return inspect_wsk(request, executor, workspace.reply);
  case ntl_net_kernel_sample::protocol::x509_issue:
    return inspect_x509(workspace.reply);
  case ntl_net_kernel_sample::protocol::schannel_client:
    return inspect_schannel_client(executor, workspace.reply);
  case ntl_net_kernel_sample::protocol::wsk_tls:
    return inspect_wsk_tls(request, executor, workspace.reply);
  case ntl_net_kernel_sample::protocol::msquic_nmr:
    return inspect_msquic_nmr(executor, workspace.reply);
  case ntl_net_kernel_sample::protocol::qpack_dynamic:
    return inspect_dynamic_qpack(workspace.reply);
  case ntl_net_kernel_sample::protocol::webtransport_backend:
    return inspect_webtransport_backend(workspace.reply);
  case ntl_net_kernel_sample::protocol::http_transform:
    return inspect_http_transform(workspace.reply);
  case ntl_net_kernel_sample::protocol::wsk_listener:
    return inspect_wsk_listener(request, workspace.reply);
  case ntl_net_kernel_sample::protocol::async_stream_state_machine:
    return inspect_async_stream_state_machine(workspace.reply);
  case ntl_net_kernel_sample::protocol::workspace_lifetime:
    return inspect_workspace_lifetime(workspace.reply);
  case ntl_net_kernel_sample::protocol::wfp_injection_lifetime:
    return inspect_injection_lifetime(workspace.reply);
  case ntl_net_kernel_sample::protocol::waitable_task_lifetime:
    return inspect_waitable_task_lifetime(workspace.reply);
  case ntl_net_kernel_sample::protocol::udp_mapping_lifetime:
    return inspect_udp_mapping_lifetime(workspace.reply);
  case ntl_net_kernel_sample::protocol::bounded_wait_set:
    return inspect_bounded_wait_set(workspace.reply);
  case ntl_net_kernel_sample::protocol::executor_lifetime:
    return inspect_executor_lifetime(workspace.reply);
  case ntl_net_kernel_sample::protocol::http3_origin_pool_lifetime:
    return inspect_http3_origin_pool_lifetime(workspace.reply);
  default:
    return STATUS_NOT_SUPPORTED;
  }
}

} // namespace

ntl::status ntl::main(ntl::driver &driver, const std::wstring &) {
  const ntl::status classification_contract =
      verify_wfp_classification_contract();
  if (!classification_contract.is_ok())
    return classification_contract;

  const ntl::status ioctl_contract = verify_ioctl_router_contract();
  if (!ioctl_contract.is_ok())
    return ioctl_contract;

  auto options = ntl::device_options()
                     .name(ntl_net_kernel_sample::device_name)
                     .type(FILE_DEVICE_UNKNOWN)
                     .exclusive(false);
  auto endpoint_result =
      ntl::try_create_device_endpoint<device_extension>(driver, options);
  if (!endpoint_result)
    return endpoint_result.status();
  auto endpoint = std::move(*endpoint_result);
  auto executor = std::make_shared<ntl::net::kernel::executor>();
  auto workspaces = std::make_shared<inspection_workspace_pool>();
  const ntl::status route_status = endpoint.on_ioctl<inspect_ioctl>(
      [executor, workspaces](const ntl_net_kernel_sample::inspect_request &request,
                             ntl_net_kernel_sample::inspect_reply &reply)
          noexcept -> ntl::status {
        try {
        auto acquired = workspaces->try_acquire();
        if (!acquired)
          return acquired.status();
        auto workspace = std::move(*acquired);
        workspace->reply.parse_status =
            static_cast<NTSTATUS>(inspect(request, *executor, *workspace));
        reply = workspace->reply;
        return ntl::status::ok();
        } catch (const ntl::exception &error) {
          return error.get_status();
        } catch (const std::bad_alloc &) {
          return STATUS_INSUFFICIENT_RESOURCES;
        } catch (...) {
          return STATUS_UNHANDLED_EXCEPTION;
        }
      });
  if (!route_status.is_ok())
    return route_status;

  const auto endpoint_copy = endpoint;
  driver.on_unload([endpoint, endpoint_copy, executor, workspaces]() mutable {
    const ntl::status closed = endpoint.close();
    NT_ASSERT(closed.is_ok());
    NT_ASSERT(!endpoint);
    NT_ASSERT(!endpoint_copy);
    const ntl::status duplicate_close = endpoint_copy.close();
    NT_ASSERT(duplicate_close.is_ok());
    executor->stop_accepting();
    const ntl::status drained = executor->drain();
    NT_ASSERT(drained.is_ok());
    workspaces->flush();
  });
  return ntl::status::ok();
}
