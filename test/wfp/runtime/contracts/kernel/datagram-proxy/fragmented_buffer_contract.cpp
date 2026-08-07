#include <ntddk.h>

#include <array>
#include <cstddef>
#include <cstring>
#include <utility>

#include <ntl/driver>
#include <ntl/wfp/all>

namespace {

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
    ntl::net::borrowed_byte_cursor cursor(bytes);
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

} // namespace

ntl::status ntl::main(ntl::driver &driver, const std::wstring &) {
  for (std::size_t split = 1; split != 8; ++split) {
    fragmented_udp_fixture fixture(driver.native_handle(), split);
    const ntl::status initialized = fixture.initialize();
    if (!initialized.is_ok())
      return initialized;
    const ntl::status validated = fixture.validate();
    if (!validated.is_ok())
      return validated;
  }
  return ntl::status::ok();
}
