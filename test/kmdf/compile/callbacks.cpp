#include <ntl/kmdf/all>

namespace ntl_kmdf_callback_compile_test {

constexpr auto io_read =
    +[](ntl::kmdf::io_queue, ntl::kmdf::request, size_t) noexcept {};
constexpr auto io_write =
    +[](ntl::kmdf::io_queue, ntl::kmdf::request, size_t) noexcept {};
constexpr auto io_control =
    +[](ntl::kmdf::io_queue, ntl::kmdf::request, size_t, size_t,
        ULONG) noexcept {};
constexpr auto io_stop =
    +[](ntl::kmdf::io_queue, ntl::kmdf::request, ULONG) noexcept {};
constexpr auto io_resume =
    +[](ntl::kmdf::io_queue, ntl::kmdf::request) noexcept {};
constexpr auto io_canceled =
    +[](ntl::kmdf::io_queue, ntl::kmdf::request) noexcept {};

constexpr auto timer_expired =
    +[](ntl::kmdf::timer) noexcept {};
constexpr auto work_item_run =
    +[](ntl::kmdf::work_item) noexcept {};
constexpr auto dpc_run =
    +[](ntl::kmdf::dpc) noexcept {};
constexpr auto request_canceled =
    +[](ntl::kmdf::request) noexcept {};

[[maybe_unused]] void compile_queue_callbacks() {
  ntl::kmdf::io_queue_config queue(WdfIoQueueDispatchParallel);
  queue.on_read<io_read>()
      .on_write<io_write>()
      .on_device_control<io_control>()
      .on_stop<io_stop>()
      .on_resume<io_resume>()
      .on_canceled<io_canceled>()
      .power_managed(WdfUseDefault)
      .parallel_requests(4);
}

[[maybe_unused]] void
compile_deferred_callbacks(ntl::kmdf::device device,
                           ntl::kmdf::request request) {
  auto timer = ntl::kmdf::timer_config::one_shot<timer_expired>();
  timer.automatic_serialization(true);
  (void)ntl::kmdf::timer::try_create(device, timer);

  auto work = ntl::kmdf::work_item_config::with_callback<work_item_run>();
  (void)ntl::kmdf::work_item::try_create(device, work);

  auto dpc = ntl::kmdf::dpc_config::with_callback<dpc_run>();
  (void)ntl::kmdf::dpc::try_create(device, dpc);

  (void)request.try_mark_cancelable<request_canceled>();
}

} // namespace ntl_kmdf_callback_compile_test
