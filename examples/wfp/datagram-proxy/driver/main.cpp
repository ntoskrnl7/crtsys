#include <ntddk.h>

#include <atomic>
#include <cstddef>
#include <cstring>
#include <memory>
#include <new>
#include <optional>

#include <ntl/driver>
#include <ntl/wfp/all>

#include "datagram_proxy_contract.hpp"

namespace {

using flow_layer = wfp_datagram_proxy::flow_layer;
using datagram_layer = wfp_datagram_proxy::datagram_layer;

class fragmented_udp_fixture {
public:
  explicit fragmented_udp_fixture(PDRIVER_OBJECT driver) noexcept
      : driver_(driver) {}

  fragmented_udp_fixture(const fragmented_udp_fixture &) = delete;
  fragmented_udp_fixture &
  operator=(const fragmented_udp_fixture &) = delete;

  ~fragmented_udp_fixture() {
    if (chain_)
      FwpsFreeNetBufferList0(chain_);
    if (pool_)
      NdisFreeNetBufferListPool(pool_);
    if (generic_)
      NdisFreeGenericObject(generic_);
    if (first_mdl_) {
      first_mdl_->Next = nullptr;
      IoFreeMdl(first_mdl_);
    }
    if (second_mdl_)
      IoFreeMdl(second_mdl_);
  }

  ntl::status initialize() noexcept {
    auto first = ntl::net::owned_bytes::try_allocate(
        3, ntl::net::buffer_limits{3}, ntl::pool_tag("fDwN"));
    auto second = ntl::net::owned_bytes::try_allocate(
        5, ntl::net::buffer_limits{5}, ntl::pool_tag("sDwN"));
    if (!first || !second)
      return STATUS_INSUFFICIENT_RESOURCES;
    first_ = std::move(*first);
    second_ = std::move(*second);

    constexpr std::byte udp[] = {
        std::byte{0x11}, std::byte{0x11}, std::byte{0x22},
        std::byte{0x22}, std::byte{0x00}, std::byte{0x08},
        std::byte{0x33}, std::byte{0x33}};
    std::memcpy(first_.data(), udp, first_.size());
    std::memcpy(second_.data(), udp + first_.size(), second_.size());

    first_mdl_ = IoAllocateMdl(
        first_.data(), static_cast<ULONG>(first_.size()), FALSE, FALSE,
        nullptr);
    second_mdl_ = IoAllocateMdl(
        second_.data(), static_cast<ULONG>(second_.size()), FALSE, FALSE,
        nullptr);
    if (!first_mdl_ || !second_mdl_)
      return STATUS_INSUFFICIENT_RESOURCES;
    MmBuildMdlForNonPagedPool(first_mdl_);
    MmBuildMdlForNonPagedPool(second_mdl_);
    first_mdl_->Next = second_mdl_;

    generic_ = NdisAllocateGenericObject(driver_, 'gDwN', 0);
    if (!generic_)
      return STATUS_INSUFFICIENT_RESOURCES;

    NET_BUFFER_LIST_POOL_PARAMETERS parameters{};
    parameters.Header.Type = NDIS_OBJECT_TYPE_DEFAULT;
    parameters.Header.Revision =
        NET_BUFFER_LIST_POOL_PARAMETERS_REVISION_1;
    parameters.Header.Size =
        NDIS_SIZEOF_NET_BUFFER_LIST_POOL_PARAMETERS_REVISION_1;
    parameters.fAllocateNetBuffer = TRUE;
    parameters.PoolTag = 'pDwN';
    pool_ = NdisAllocateNetBufferListPool(generic_, &parameters);
    if (!pool_)
      return STATUS_INSUFFICIENT_RESOURCES;

    return FwpsAllocateNetBufferAndNetBufferList0(
        pool_, 0, 0, first_mdl_, 0,
        static_cast<SIZE_T>(first_.size() + second_.size()), &chain_);
  }

  ntl::status validate() noexcept {
    auto editable = ntl::wfp::detail::mutable_nbl_bytes(chain_);
    if (!editable || editable.size() != 8)
      return STATUS_DATA_ERROR;
    const ntl::status destination = editable.write_be16(2, 0x4444);
    const ntl::status checksum = editable.write_be16(6, 0);
    if (!destination.is_ok() || !checksum.is_ok())
      return STATUS_DATA_ERROR;

    const auto bytes = ntl::wfp::detail::nbl_bytes(chain_);
    ntl::net::byte_cursor cursor(bytes);
    const auto source_port = cursor.read_be16();
    const auto destination_port = cursor.read_be16();
    const auto length = cursor.read_be16();
    const auto checksum_value = cursor.read_be16();
    if (!source_port || *source_port != 0x1111 ||
        !destination_port || *destination_port != 0x4444 ||
        !length || *length != 8 ||
        !checksum_value || *checksum_value != 0)
      return STATUS_DATA_ERROR;

    const auto rejected = ntl::net::owned_bytes::try_copy(
        bytes, ntl::net::buffer_limits{7}, ntl::pool_tag("rDwN"));
    if (rejected ||
        static_cast<NTSTATUS>(rejected.status()) !=
            STATUS_BUFFER_OVERFLOW)
      return STATUS_DATA_ERROR;
    const auto copied = ntl::net::owned_bytes::try_copy(
        bytes, ntl::net::buffer_limits{8}, ntl::pool_tag("cDwN"));
    return copied && copied->size() == 8
               ? ntl::status::ok()
               : ntl::status{STATUS_DATA_ERROR};
  }

private:
  PDRIVER_OBJECT driver_ = nullptr;
  ntl::net::owned_bytes first_;
  ntl::net::owned_bytes second_;
  MDL *first_mdl_ = nullptr;
  MDL *second_mdl_ = nullptr;
  PNDIS_GENERIC_OBJECT generic_ = nullptr;
  NDIS_HANDLE pool_ = nullptr;
  NET_BUFFER_LIST *chain_ = nullptr;
};

ntl::status validate_fragmented_udp_edit(PDRIVER_OBJECT driver) noexcept {
  fragmented_udp_fixture fixture(driver);
  const ntl::status initialized = fixture.initialize();
  return initialized.is_ok() ? fixture.validate() : initialized;
}

struct proxy_flow {
  explicit proxy_flow(std::uint16_t port) noexcept : target_port(port) {}
  std::uint16_t target_port;
};

struct proxy_state {
  explicit proxy_state(ntl::wfp::transport_injector &&value) noexcept
      : injector(std::move(value)) {}

  ntl::wfp::transport_injector injector;
  std::optional<ntl::wfp::flow_target<datagram_layer, proxy_flow>> target;
  std::atomic<std::uint64_t> redirected{0};
  std::atomic<std::uint64_t> failures{0};
};

proxy_state *g_state = nullptr;

constexpr auto redirect_datagram =
    +[](const ntl::wfp::classify_event<datagram_layer> &event,
        proxy_flow *flow) noexcept {
      proxy_state *const state = g_state;
      const auto packet = event.packet();
      if (!state || !packet)
        return ntl::wfp::decision::block_and_absorb;

      const auto injection_state = state->injector.query(packet);
      if (injection_state == FWPS_PACKET_INJECTED_BY_SELF ||
          injection_state == FWPS_PACKET_PREVIOUSLY_INJECTED_BY_SELF)
        return ntl::wfp::decision::permit;

      const auto direction =
          event.value(datagram_layer::field::direction).uint32();
      const auto protocol =
          event.value(datagram_layer::field::protocol).uint8();
      const auto remote_address =
          event.value(datagram_layer::field::remote_address).uint32();
      const auto endpoint =
          event.metadata().transport_endpoint_handle();
      const auto compartment = event.metadata().compartment_id();
      if (!flow || !direction || !protocol || !remote_address ||
          !endpoint || !compartment ||
          *direction != FWP_DIRECTION_OUTBOUND ||
          *protocol != IPPROTO_UDP) {
        state->failures.fetch_add(1, std::memory_order_relaxed);
        return ntl::wfp::decision::block_and_absorb;
      }

      auto clone = ntl::wfp::cloned_packet::try_create(packet);
      if (!clone ||
          !clone->rewrite_udp_destination_port(flow->target_port).is_ok()) {
        state->failures.fetch_add(1, std::memory_order_relaxed);
        return ntl::wfp::decision::block_and_absorb;
      }

      std::uint32_t remote_network_order =
          RtlUlongByteSwap(*remote_address);
      FWPS_TRANSPORT_SEND_PARAMS0 parameters{};
      parameters.remoteAddress =
          reinterpret_cast<UINT8 *>(&remote_network_order);

      const ntl::status injected = state->injector.inject_send(
          std::move(*clone), *endpoint, AF_INET,
          static_cast<COMPARTMENT_ID>(*compartment), &parameters);
      if (!injected.is_ok()) {
        state->failures.fetch_add(1, std::memory_order_relaxed);
        return ntl::wfp::decision::block_and_absorb;
      }

      state->redirected.fetch_add(1, std::memory_order_relaxed);
      return ntl::wfp::decision::block_and_absorb;
    };

constexpr auto remember_proxy_flow =
    +[](const ntl::wfp::classify_event<flow_layer> &event) noexcept {
      proxy_state *const state = g_state;
      if (!state || !state->target)
        return ntl::wfp::decision::continue_classification;

      const auto flow_handle = event.metadata().flow_handle();
      const auto protocol =
          event.value(flow_layer::field::protocol).uint8();
      const auto direction =
          event.value(flow_layer::field::direction).uint32();
      const auto target_port = event.filter().context();
      if (!flow_handle || !protocol || !direction ||
          *protocol != IPPROTO_UDP ||
          *direction != FWP_DIRECTION_OUTBOUND ||
          target_port == 0 || target_port > MAXUSHORT)
        return ntl::wfp::decision::continue_classification;

      std::unique_ptr<proxy_flow> context(
          new (std::nothrow)
              proxy_flow(static_cast<std::uint16_t>(target_port)));
      if (!context)
        return ntl::wfp::decision::continue_classification;

      (void)state->target->associate(*flow_handle, std::move(context));
      return ntl::wfp::decision::continue_classification;
    };

} // namespace

ntl::status ntl::main(ntl::driver &driver, const std::wstring &) {
  const ntl::status buffer_contract =
      validate_fragmented_udp_edit(driver.native_handle());
  if (!buffer_contract.is_ok())
    return buffer_contract;

  auto injector = ntl::wfp::transport_injector::try_create(AF_INET);
  if (!injector)
    return injector.status();

  auto state =
      std::make_shared<proxy_state>(std::move(*injector));
  auto callouts = std::make_shared<ntl::wfp::callout_driver<>>(driver);
  g_state = state.get();

  auto target =
      callouts->add_flow_context<proxy_flow, redirect_datagram>(
          wfp_datagram_proxy::datagram_callout_key);
  if (!target) {
    g_state = nullptr;
    return target.status();
  }
  state->target = *target;

  const ntl::status flow_status =
      callouts->add<remember_proxy_flow>(
          wfp_datagram_proxy::flow_callout_key);
  if (!flow_status.is_ok()) {
    g_state = nullptr;
    (void)callouts->reset();
    return flow_status;
  }

  driver.on_unload([state, callouts] {
    const ntl::status result = callouts->reset();
    NT_ASSERT(result.is_ok());
    state->injector.reset();
    g_state = nullptr;
  });
  return ntl::status::ok();
}
