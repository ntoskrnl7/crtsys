#include <ntl/kmdf/all>

#include <type_traits>
#include <utility>

namespace ntl_kmdf_hardware_compile_test {

struct child_identity {
  ULONG serial;
};

static_assert(std::is_trivially_copyable_v<child_identity>);
static_assert(
    std::is_same_v<
        decltype(ntl::kmdf::usb_device::try_create(
            std::declval<const ntl::kmdf::device &>(),
            std::declval<ntl::kmdf::usb_device_create_config &>())),
        ntl::result<ntl::kmdf::usb_device>>);
static_assert(
    std::is_same_v<
        decltype(ntl::kmdf::dma_enabler::try_create(
            std::declval<const ntl::kmdf::device &>(),
            std::declval<ntl::kmdf::dma_enabler_config &>())),
        ntl::result<ntl::kmdf::dma_enabler>>);
static_assert(
    std::is_same_v<
        decltype(ntl::kmdf::pdo_init::try_allocate(
            std::declval<const ntl::kmdf::device &>())),
        ntl::result<ntl::kmdf::pdo_init>>);

constexpr auto interrupt_service =
    +[](ntl::kmdf::interrupt, ULONG) noexcept {
  return false;
};
constexpr auto interrupt_dpc =
    +[](ntl::kmdf::interrupt, ntl::kmdf::object) noexcept {};

constexpr auto child_create =
    +[](ntl::kmdf::child_list,
        const ntl::kmdf::child_identification<child_identity> &,
        ntl::kmdf::pdo_init) noexcept -> NTSTATUS {
  return STATUS_SUCCESS;
};

[[maybe_unused]] void compile_hardware_callbacks() {
  auto interrupt =
      ntl::kmdf::interrupt_config::with_isr<interrupt_service>();
  interrupt.on_dpc<interrupt_dpc>();

  using children =
      ntl::kmdf::child_list_config<child_identity>;
  auto child_config = children::with_create<child_create>();
  (void)child_config;
}

} // namespace ntl_kmdf_hardware_compile_test
