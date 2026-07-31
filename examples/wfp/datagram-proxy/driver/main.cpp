#include <ntddk.h>

#include <array>
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

using flow_layer_v4 = wfp_datagram_proxy::flow_layer_v4;
using flow_layer_v6 = wfp_datagram_proxy::flow_layer_v6;
using datagram_layer_v4 = wfp_datagram_proxy::datagram_layer_v4;
using datagram_layer_v6 = wfp_datagram_proxy::datagram_layer_v6;

class fragmented_udp_fixture {
public:
  fragmented_udp_fixture(PDRIVER_OBJECT driver,
                         std::size_t split) noexcept
      : driver_(driver), split_(split) {}

  fragmented_udp_fixture(const fragmented_udp_fixture &) = delete;
  fragmented_udp_fixture &operator=(const fragmented_udp_fixture &) = delete;

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
        split_, ntl::net::buffer_limits{split_}, ntl::pool_tag("fDwN"));
    auto second = ntl::net::owned_bytes::try_allocate(
        8 - split_, ntl::net::buffer_limits{8 - split_},
        ntl::pool_tag("sDwN"));
    if (!first || !second)
      return STATUS_INSUFFICIENT_RESOURCES;
    first_ = std::move(*first);
    second_ = std::move(*second);

    constexpr std::byte udp[] = {
        std::byte{0x11}, std::byte{0x11}, std::byte{0x22}, std::byte{0x22},
        std::byte{0x00}, std::byte{0x08}, std::byte{0x33}, std::byte{0x33}};
    std::memcpy(first_.data(), udp, first_.size());
    std::memcpy(second_.data(), udp + first_.size(), second_.size());

    first_mdl_ = IoAllocateMdl(first_.data(), static_cast<ULONG>(first_.size()),
                               FALSE, FALSE, nullptr);
    second_mdl_ =
        IoAllocateMdl(second_.data(), static_cast<ULONG>(second_.size()), FALSE,
                      FALSE, nullptr);
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
    parameters.Header.Revision = NET_BUFFER_LIST_POOL_PARAMETERS_REVISION_1;
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
    const ntl::status destination_and_length =
        editable.write_be32(2, 0x44440008);
    const ntl::status checksum = editable.write_be16(6, 0);
    if (!destination_and_length.is_ok() || !checksum.is_ok())
      return STATUS_DATA_ERROR;

    const auto bytes = ntl::wfp::detail::nbl_bytes(chain_);
    ntl::net::byte_cursor cursor(bytes);
    const auto source_port = cursor.read_be16();
    const auto destination_port = cursor.read_be16();
    const auto length = cursor.read_be16();
    const auto checksum_value = cursor.read_be16();
    if (!source_port || *source_port != 0x1111 || !destination_port ||
        *destination_port != 0x4444 || !length || *length != 8 ||
        !checksum_value || *checksum_value != 0)
      return STATUS_DATA_ERROR;

    const auto middle = ntl::wfp::detail::nbl_bytes(chain_, 1, 6);
    const auto middle_value = middle.read<std::uint32_t>(1);
    if (!middle || middle.size() != 6 || !middle_value)
      return STATUS_DATA_ERROR;
    std::array<std::byte, 6> middle_copy{};
    if (!middle.copy_to(middle_copy).is_ok() ||
        middle_copy[1] != std::byte{0x44} ||
        middle_copy[2] != std::byte{0x44} ||
        middle_copy[3] != std::byte{0x00} ||
        middle_copy[4] != std::byte{0x08})
      return STATUS_DATA_ERROR;

    const auto rejected = ntl::net::owned_bytes::try_copy(
        bytes, ntl::net::buffer_limits{7}, ntl::pool_tag("rDwN"));
    if (rejected ||
        static_cast<NTSTATUS>(rejected.status()) != STATUS_BUFFER_OVERFLOW)
      return STATUS_DATA_ERROR;
    const auto copied = ntl::net::owned_bytes::try_copy(
        bytes, ntl::net::buffer_limits{8}, ntl::pool_tag("cDwN"));
    return copied && copied->size() == 8 ? ntl::status::ok()
                                         : ntl::status{STATUS_DATA_ERROR};
  }

private:
  PDRIVER_OBJECT driver_ = nullptr;
  std::size_t split_ = 0;
  ntl::net::owned_bytes first_;
  ntl::net::owned_bytes second_;
  MDL *first_mdl_ = nullptr;
  MDL *second_mdl_ = nullptr;
  PNDIS_GENERIC_OBJECT generic_ = nullptr;
  NDIS_HANDLE pool_ = nullptr;
  NET_BUFFER_LIST *chain_ = nullptr;
};

ntl::status validate_fragmented_udp_edit(PDRIVER_OBJECT driver) noexcept {
  for (std::size_t split = 1; split != 8; ++split) {
    fragmented_udp_fixture fixture(driver, split);
    const ntl::status initialized = fixture.initialize();
    if (!initialized.is_ok())
      return initialized;
    const ntl::status validated = fixture.validate();
    if (!validated.is_ok())
      return validated;
  }
  return ntl::status::ok();
}

struct proxy_flow {
  explicit proxy_flow(std::uint16_t port) noexcept : target_port(port) {}
  std::uint16_t target_port;
};

struct proxy_state {
  proxy_state(ntl::wfp::transport_injector &&value_v4,
              ntl::wfp::transport_injector &&value_v6) noexcept
      : injector_v4(std::move(value_v4)), injector_v6(std::move(value_v6)) {}

  ntl::wfp::transport_injector injector_v4;
  ntl::wfp::transport_injector injector_v6;
  std::optional<ntl::wfp::flow_target<datagram_layer_v4, proxy_flow>> target_v4;
  std::optional<ntl::wfp::flow_target<datagram_layer_v6, proxy_flow>> target_v6;
  std::atomic<std::uint64_t> redirected{0};
  std::atomic<std::uint64_t> failures{0};
};

proxy_state *g_state = nullptr;

template <class DatagramLayer>
ntl::wfp::decision
redirect_datagram(const ntl::wfp::classify_event<DatagramLayer> &event,
                  proxy_flow *flow) noexcept {
  proxy_state *const state = g_state;
  const auto packet = event.packet();
  if (!state || !packet)
    return ntl::wfp::decision::block_and_absorb;

  auto &injector = [&]() -> ntl::wfp::transport_injector & {
    if constexpr (std::is_same_v<DatagramLayer, datagram_layer_v4>)
      return state->injector_v4;
    else
      return state->injector_v6;
  }();
  const auto injection_state = injector.query(packet);
  if (injection_state == FWPS_PACKET_INJECTED_BY_SELF ||
      injection_state == FWPS_PACKET_PREVIOUSLY_INJECTED_BY_SELF)
    return ntl::wfp::decision::permit;

  const auto direction = event.value(DatagramLayer::field::direction).uint32();
  const auto protocol = event.value(DatagramLayer::field::protocol).uint8();
  const auto endpoint = event.metadata().transport_endpoint_handle();
  const auto compartment = event.metadata().compartment_id();
  if (!flow || !direction || !protocol || !endpoint || !compartment ||
      *direction != FWP_DIRECTION_OUTBOUND || *protocol != IPPROTO_UDP) {
    state->failures.fetch_add(1, std::memory_order_relaxed);
    return ntl::wfp::decision::block_and_absorb;
  }

  auto clone = ntl::wfp::cloned_packet::try_create(packet);
  if (!clone ||
      !clone->rewrite_udp_destination_port(flow->target_port).is_ok()) {
    state->failures.fetch_add(1, std::memory_order_relaxed);
    return ntl::wfp::decision::block_and_absorb;
  }

  FWPS_TRANSPORT_SEND_PARAMS0 parameters{};
  ADDRESS_FAMILY family = AF_UNSPEC;
  std::uint32_t remote_v4 = 0;
  std::array<UINT8, 16> remote_v6{};
  if constexpr (std::is_same_v<DatagramLayer, datagram_layer_v4>) {
    const auto address =
        event.value(DatagramLayer::field::remote_address).uint32();
    if (!address) {
      state->failures.fetch_add(1, std::memory_order_relaxed);
      return ntl::wfp::decision::block_and_absorb;
    }
    remote_v4 = RtlUlongByteSwap(*address);
    parameters.remoteAddress = reinterpret_cast<UINT8 *>(&remote_v4);
    family = AF_INET;
  } else {
    const auto *address =
        event.value(DatagramLayer::field::remote_address).byte_array16();
    if (!address) {
      state->failures.fetch_add(1, std::memory_order_relaxed);
      return ntl::wfp::decision::block_and_absorb;
    }
    std::memcpy(remote_v6.data(), address->byteArray16, remote_v6.size());
    parameters.remoteAddress = remote_v6.data();
    family = AF_INET6;
  }

  const ntl::status injected = injector.inject_send(
      std::move(*clone), *endpoint, family,
      static_cast<COMPARTMENT_ID>(*compartment), &parameters);
  if (!injected.is_ok()) {
    state->failures.fetch_add(1, std::memory_order_relaxed);
    return ntl::wfp::decision::block_and_absorb;
  }

  state->redirected.fetch_add(1, std::memory_order_relaxed);
  return ntl::wfp::decision::block_and_absorb;
}

template <class FlowLayer, class DatagramLayer>
ntl::wfp::decision
remember_proxy_flow(const ntl::wfp::classify_event<FlowLayer> &event) noexcept {
  proxy_state *const state = g_state;
  if (!state)
    return ntl::wfp::decision::continue_classification;

  auto &target = [&]() -> auto & {
    if constexpr (std::is_same_v<DatagramLayer, datagram_layer_v4>)
      return state->target_v4;
    else
      return state->target_v6;
  }();
  if (!target)
    return ntl::wfp::decision::continue_classification;

  const auto flow_handle = event.metadata().flow_handle();
  const auto protocol = event.value(FlowLayer::field::protocol).uint8();
  const auto direction = event.value(FlowLayer::field::direction).uint32();
  const auto target_port = event.filter().context();
  if (!flow_handle || !protocol || !direction || *protocol != IPPROTO_UDP ||
      *direction != FWP_DIRECTION_OUTBOUND || target_port == 0 ||
      target_port > MAXUSHORT)
    return ntl::wfp::decision::continue_classification;

  std::unique_ptr<proxy_flow> context(
      new (std::nothrow) proxy_flow(static_cast<std::uint16_t>(target_port)));
  if (!context)
    return ntl::wfp::decision::continue_classification;

  (void)target->associate(*flow_handle, std::move(context));
  return ntl::wfp::decision::continue_classification;
}

} // namespace

ntl::status ntl::main(ntl::driver &driver, const std::wstring &) {
  const ntl::status buffer_contract =
      validate_fragmented_udp_edit(driver.native_handle());
  if (!buffer_contract.is_ok())
    return buffer_contract;

  auto injector_v4 = ntl::wfp::transport_injector::try_create(AF_INET);
  if (!injector_v4)
    return injector_v4.status();
  auto injector_v6 = ntl::wfp::transport_injector::try_create(AF_INET6);
  if (!injector_v6)
    return injector_v6.status();

  auto state = std::make_shared<proxy_state>(std::move(*injector_v4),
                                             std::move(*injector_v6));
  auto callouts = std::make_shared<ntl::wfp::callout_driver<>>(driver);
  g_state = state.get();

  auto target_v4 =
      callouts
          ->add_flow_context<proxy_flow, redirect_datagram<datagram_layer_v4>>(
              wfp_datagram_proxy::datagram_callout_key_v4);
  if (!target_v4) {
    g_state = nullptr;
    return target_v4.status();
  }
  state->target_v4 = *target_v4;

  auto target_v6 =
      callouts
          ->add_flow_context<proxy_flow, redirect_datagram<datagram_layer_v6>>(
              wfp_datagram_proxy::datagram_callout_key_v6);
  if (!target_v6) {
    g_state = nullptr;
    (void)callouts->reset();
    return target_v6.status();
  }
  state->target_v6 = *target_v6;

  const ntl::status flow_status_v4 =
      callouts->add<remember_proxy_flow<flow_layer_v4, datagram_layer_v4>>(
          wfp_datagram_proxy::flow_callout_key_v4);
  if (!flow_status_v4.is_ok()) {
    g_state = nullptr;
    (void)callouts->reset();
    return flow_status_v4;
  }
  const ntl::status flow_status_v6 =
      callouts->add<remember_proxy_flow<flow_layer_v6, datagram_layer_v6>>(
          wfp_datagram_proxy::flow_callout_key_v6);
  if (!flow_status_v6.is_ok()) {
    g_state = nullptr;
    (void)callouts->reset();
    return flow_status_v6;
  }

  driver.on_unload([state, callouts] {
    const ntl::status result = callouts->reset();
    NT_ASSERT(result.is_ok());
    state->injector_v4.reset();
    state->injector_v6.reset();
    g_state = nullptr;
  });
  return ntl::status::ok();
}
