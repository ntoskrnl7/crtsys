#include <ntddk.h>

#include <array>
#include <atomic>
#include <chrono>
#include <coroutine>
#include <cstddef>
#include <cstring>
#include <limits>
#include <memory>
#include <new>
#include <optional>
#include <span>
#include <type_traits>
#include <utility>

#include <ntl/device_endpoint>
#include <ntl/driver>
#include <ntl/except>
#include <ntl/ioctl>
#include <ntl/wfp/all>

#include "flow_monitor_contract.hpp"

namespace {

using query_stats =
    ntl::ioctl_from_contract<wfp_flow_monitor::query_stats_contract>;

#if NTL_HAS_COROUTINE_SUPPORT
class reader_test_task {
public:
  struct promise_type;
  using handle_type = std::coroutine_handle<promise_type>;

  struct promise_type {
    promise_type() noexcept {
      KeInitializeEvent(&completed, NotificationEvent, FALSE);
    }

    reader_test_task get_return_object() noexcept {
      return reader_test_task(handle_type::from_promise(*this));
    }
    std::suspend_always initial_suspend() const noexcept { return {}; }

    struct final_awaiter {
      bool await_ready() const noexcept { return false; }
      void await_suspend(handle_type handle) const noexcept {
        KeSetEvent(&handle.promise().completed, IO_NO_INCREMENT, FALSE);
      }
      void await_resume() const noexcept {}
    };

    final_awaiter final_suspend() const noexcept { return {}; }
    void return_value(NTSTATUS value) noexcept { result = value; }
    void unhandled_exception() noexcept { result = STATUS_UNSUCCESSFUL; }

    KEVENT completed{};
    NTSTATUS result = STATUS_PENDING;
  };

  reader_test_task(const reader_test_task &) = delete;
  reader_test_task &operator=(const reader_test_task &) = delete;

  reader_test_task(reader_test_task &&other) noexcept
      : handle_(std::exchange(other.handle_, {})) {}

  reader_test_task &operator=(reader_test_task &&other) noexcept {
    if (this != &other) {
      reset();
      handle_ = std::exchange(other.handle_, {});
    }
    return *this;
  }

  ~reader_test_task() { reset(); }

  void start() noexcept {
    if (handle_ && !handle_.done())
      handle_.resume();
  }

  ntl::status wait(std::chrono::milliseconds timeout) noexcept {
    if (!handle_)
      return STATUS_INVALID_DEVICE_STATE;
    const auto maximum = (std::numeric_limits<LONGLONG>::max)() / 10000;
    const auto count = timeout.count();
    const LONGLONG milliseconds =
        count > maximum ? maximum : static_cast<LONGLONG>(count);
    LARGE_INTEGER interval{};
    interval.QuadPart = -milliseconds * 10000;
    return KeWaitForSingleObject(&handle_.promise().completed, Executive,
                                 KernelMode, FALSE, &interval);
  }

  NTSTATUS result() const noexcept {
    return handle_ ? handle_.promise().result : STATUS_INVALID_DEVICE_STATE;
  }

  bool done() const noexcept { return handle_ && handle_.done(); }

private:
  explicit reader_test_task(handle_type handle) noexcept : handle_(handle) {}

  void reset() noexcept {
    const handle_type handle = std::exchange(handle_, {});
    if (handle) {
      NT_ASSERT(handle.done());
      if (handle.done())
        handle.destroy();
    }
  }

  handle_type handle_{};
};

reader_test_task read_fragmented_message(ntl::net::async_byte_stream &stream,
                                         KEVENT &waiting_for_body) {
  auto header = co_await stream.read_exactly<std::array<std::byte, 4>>(
      {std::chrono::milliseconds(1000)});
  if (!header)
    co_return static_cast<NTSTATUS>(header.status());

  ntl::net::byte_cursor cursor(
      ntl::net::scatter_view::from_contiguous(*header));
  const auto magic = cursor.read_be16();
  const auto length = cursor.read_be16();
  if (!magic || *magic != 0xcafe || !length || *length > 8)
    co_return STATUS_DATA_ERROR;

  auto body = ntl::net::owned_bytes::try_allocate(
      *length, ntl::net::buffer_limits{8}, ntl::pool_tag("bTwN"));
  if (!body)
    co_return static_cast<NTSTATUS>(body.status());

  KeSetEvent(&waiting_for_body, IO_NO_INCREMENT, FALSE);
  const ntl::status read = co_await stream.read_exactly(
      body->span(), {std::chrono::milliseconds(1000)});
  if (!read.is_ok())
    co_return static_cast<NTSTATUS>(read);

  constexpr std::byte expected[] = {std::byte{'a'}, std::byte{'b'},
                                    std::byte{'c'}};
  if (body->size() != sizeof(expected) ||
      std::memcmp(body->data(), expected, sizeof(expected)) != 0)
    co_return STATUS_DATA_ERROR;
  co_return STATUS_SUCCESS;
}

reader_test_task read_expected_timeout(ntl::net::async_byte_stream &stream) {
  std::array<std::byte, 1> byte{};
  const ntl::status read = co_await stream.read_exactly(
      std::span<std::byte>(byte), {std::chrono::milliseconds(20)});
  co_return static_cast<NTSTATUS>(read) == STATUS_IO_TIMEOUT
      ? STATUS_SUCCESS
      : STATUS_DATA_ERROR;
}

reader_test_task read_expected_status(ntl::net::async_byte_stream &stream,
                                      std::size_t count, NTSTATUS expected) {
  auto bytes = ntl::net::owned_bytes::try_allocate(
      count, ntl::net::buffer_limits{16}, ntl::pool_tag("eTwN"));
  if (!bytes)
    co_return static_cast<NTSTATUS>(bytes.status());
  const ntl::status read = co_await stream.read_exactly(
      bytes->span(), {std::chrono::milliseconds(1000)});
  co_return static_cast<NTSTATUS>(read) == expected ? STATUS_SUCCESS
                                                    : STATUS_DATA_ERROR;
}

ntl::status wait_reader_task(reader_test_task &task,
                             ntl::net::async_byte_stream &stream) noexcept {
  ntl::status waited = task.wait(std::chrono::milliseconds(5000));
  if (static_cast<NTSTATUS>(waited) != STATUS_SUCCESS) {
    stream.cancel();
    (void)stream.cancel_and_wait();
    waited = task.wait(std::chrono::milliseconds(5000));
  }
  if (static_cast<NTSTATUS>(waited) != STATUS_SUCCESS || !task.done())
    return STATUS_IO_TIMEOUT;
  return task.result();
}

ntl::status validate_coroutine_stream_reader() noexcept {
  auto reader_result = ntl::net::async_byte_stream::try_create(16);
  if (!reader_result)
    return reader_result.status();
  auto reader = std::move(*reader_result);
  KEVENT waiting_for_body{};
  KeInitializeEvent(&waiting_for_body, NotificationEvent, FALSE);
  auto task = read_fragmented_message(reader, waiting_for_body);
  task.start();

  constexpr std::array<std::byte, 1> first{std::byte{0xca}};
  constexpr std::array<std::byte, 3> second{std::byte{0xfe}, std::byte{0x00},
                                            std::byte{0x03}};
  constexpr std::array<std::byte, 1> third{std::byte{'a'}};
  constexpr std::array<std::byte, 2> fourth{std::byte{'b'}, std::byte{'c'}};
  const ntl::status first_status = reader.append_received_data(
      ntl::net::scatter_view::from_contiguous(first));
  const ntl::status second_status = reader.append_received_data(
      ntl::net::scatter_view::from_contiguous(second));

  LARGE_INTEGER body_wait_timeout{};
  body_wait_timeout.QuadPart = -5LL * 1000 * 1000 * 10;
  const NTSTATUS body_wait = KeWaitForSingleObject(
      &waiting_for_body, Executive, KernelMode, FALSE, &body_wait_timeout);
  const ntl::status third_status = reader.append_received_data(
      ntl::net::scatter_view::from_contiguous(third));
  const ntl::status fourth_status = reader.append_received_data(
      ntl::net::scatter_view::from_contiguous(fourth));
  if (!first_status.is_ok() || !second_status.is_ok() ||
      body_wait != STATUS_SUCCESS || !third_status.is_ok() ||
      !fourth_status.is_ok()) {
    reader.cancel();
  }

  const ntl::status fragmented = wait_reader_task(task, reader);
  const ntl::status drained = reader.cancel_and_wait();
  if (!first_status.is_ok() || !second_status.is_ok() ||
      body_wait != STATUS_SUCCESS || !third_status.is_ok() ||
      !fourth_status.is_ok() || !fragmented.is_ok() || !drained.is_ok())
    return STATUS_DATA_ERROR;

  auto timeout_reader_result = ntl::net::async_byte_stream::try_create(8);
  if (!timeout_reader_result)
    return timeout_reader_result.status();
  auto timeout_reader = std::move(*timeout_reader_result);
  auto timeout_task = read_expected_timeout(timeout_reader);
  timeout_task.start();
  const ntl::status timed_out = wait_reader_task(timeout_task, timeout_reader);
  const ntl::status timeout_drained = timeout_reader.cancel_and_wait();
  if (!timed_out.is_ok() || !timeout_drained.is_ok())
    return STATUS_DATA_ERROR;

  auto single_reader_result = ntl::net::async_byte_stream::try_create(4);
  if (!single_reader_result)
    return single_reader_result.status();
  auto single_reader = std::move(*single_reader_result);
  auto first_waiter = read_expected_status(single_reader, 1, STATUS_CANCELLED);
  auto competing_waiter =
      read_expected_status(single_reader, 1, STATUS_DEVICE_BUSY);
  first_waiter.start();
  competing_waiter.start();
  const ntl::status competing =
      wait_reader_task(competing_waiter, single_reader);
  single_reader.cancel();
  const ntl::status cancelled = wait_reader_task(first_waiter, single_reader);
  const ntl::status single_reader_drained = single_reader.cancel_and_wait();
  if (!competing.is_ok() || !cancelled.is_ok() ||
      !single_reader_drained.is_ok())
    return STATUS_DATA_ERROR;

  auto eof_reader_result = ntl::net::async_byte_stream::try_create(4);
  if (!eof_reader_result)
    return eof_reader_result.status();
  auto eof_reader = std::move(*eof_reader_result);
  auto eof_task = read_expected_status(eof_reader, 2, STATUS_END_OF_FILE);
  eof_task.start();
  constexpr std::array<std::byte, 1> partial{std::byte{0x7f}};
  const ntl::status partial_status = eof_reader.append_received_data(
      ntl::net::scatter_view::from_contiguous(partial));
  eof_reader.close();
  const ntl::status eof = wait_reader_task(eof_task, eof_reader);
  const ntl::status eof_drained = eof_reader.cancel_and_wait();
  if (!partial_status.is_ok() || !eof.is_ok() || !eof_drained.is_ok())
    return STATUS_DATA_ERROR;

  auto limit_reader_result = ntl::net::async_byte_stream::try_create(4);
  if (!limit_reader_result)
    return limit_reader_result.status();
  auto limit_reader = std::move(*limit_reader_result);
  constexpr std::array<std::byte, 5> oversized{};
  const ntl::status overflow = limit_reader.append_received_data(
      ntl::net::scatter_view::from_contiguous(oversized));
  const ntl::status limit_drained = limit_reader.cancel_and_wait();
  return static_cast<NTSTATUS>(overflow) == STATUS_BUFFER_OVERFLOW &&
                 limit_drained.is_ok()
             ? ntl::status::ok()
             : ntl::status{STATUS_DATA_ERROR};
}
#endif

struct monitor_state;

struct monitor_flow {
  explicit monitor_flow(monitor_state &value) noexcept : state(&value) {}
  ~monitor_flow() noexcept;
  monitor_state *state;
};

struct monitor_state {
  std::optional<
      ntl::wfp::flow_target<wfp_flow_monitor::stream_layer_v4, monitor_flow>>
      target_v4;
  std::optional<
      ntl::wfp::flow_target<wfp_flow_monitor::stream_layer_v6, monitor_flow>>
      target_v6;
  std::atomic<std::uint64_t> flows_started{0};
  std::atomic<std::uint64_t> flows_closed{0};
  std::atomic<std::uint64_t> stream_indications{0};
  std::atomic<std::uint64_t> stream_bytes{0};
  std::atomic<std::uint64_t> missed_bytes{0};

  wfp_flow_monitor::monitor_stats snapshot() const noexcept {
    return {
        flows_started.load(std::memory_order_relaxed),
        flows_closed.load(std::memory_order_relaxed),
        stream_indications.load(std::memory_order_relaxed),
        stream_bytes.load(std::memory_order_relaxed),
        missed_bytes.load(std::memory_order_relaxed),
    };
  }
};

monitor_flow::~monitor_flow() noexcept {
  state->flows_closed.fetch_add(1, std::memory_order_relaxed);
}

monitor_state *g_state = nullptr;

template <class StreamLayer>
ntl::wfp::stream_result observe_stream(
    const ntl::wfp::stream_event<StreamLayer, monitor_flow> &event) noexcept {
  monitor_flow *const flow = event.context();
  const auto data = event.data();
  if (flow) {
    flow->state->stream_indications.fetch_add(1, std::memory_order_relaxed);
    flow->state->stream_bytes.fetch_add(data.size(), std::memory_order_relaxed);
    flow->state->missed_bytes.fetch_add(event.missed_bytes(),
                                        std::memory_order_relaxed);
  }
  return ntl::wfp::stream_result::permit(data.size());
}

template <class FlowLayer, class StreamLayer>
ntl::wfp::decision
begin_flow(const ntl::wfp::classify_event<FlowLayer> &event) noexcept {
  monitor_state *const state = g_state;
  if (!state)
    return ntl::wfp::decision::continue_classification;

  auto &target = [&]() -> auto & {
    if constexpr (std::is_same_v<StreamLayer,
                                 wfp_flow_monitor::stream_layer_v4>)
      return state->target_v4;
    else
      return state->target_v6;
  }();
  if (!target)
    return ntl::wfp::decision::continue_classification;

  const auto handle = event.metadata().flow_handle();
  const auto protocol = event.value(FlowLayer::field::protocol).uint8();
  const auto direction = event.value(FlowLayer::field::direction).uint32();
  if (!handle || !protocol || !direction || *protocol != IPPROTO_TCP ||
      *direction != FWP_DIRECTION_OUTBOUND)
    return ntl::wfp::decision::continue_classification;

  std::unique_ptr<monitor_flow> context(new (std::nothrow)
                                            monitor_flow(*state));
  if (!context)
    return ntl::wfp::decision::continue_classification;

  const ntl::status associated = target->associate(*handle, std::move(context));
  if (associated.is_ok())
    state->flows_started.fetch_add(1, std::memory_order_relaxed);
  return ntl::wfp::decision::continue_classification;
}

} // namespace

ntl::status ntl::main(ntl::driver &driver, const std::wstring &) {
#if NTL_HAS_COROUTINE_SUPPORT
  const ntl::status reader_contract = validate_coroutine_stream_reader();
  if (!reader_contract.is_ok())
    return reader_contract;
#endif

  auto state = std::make_shared<monitor_state>();

  auto options = ntl::device_options()
                     .name(wfp_flow_monitor::device_name)
                     .type(FILE_DEVICE_UNKNOWN)
                     .exclusive(false)
                     .security_descriptor(L"D:P(A;;GA;;;SY)(A;;GA;;;BA)",
                                          wfp_flow_monitor::device_class_guid);
  auto endpoint_result = ntl::try_create_device_endpoint<void>(driver, options);
  if (!endpoint_result)
    return endpoint_result.status();
  auto endpoint =
      std::make_shared<ntl::device_endpoint<void>>(std::move(*endpoint_result));
  auto device = endpoint->device();
  if (!device)
    return STATUS_INVALID_DEVICE_STATE;

  device->on_create([](ntl::irp &request) { request.succeed(); });
  device->on_close([](ntl::irp &request) { request.succeed(); });
  device->on_device_control([state](const ntl::device_control::code &code,
                                    const ntl::device_control::in_buffer &,
                                    ntl::device_control::out_buffer &out) {
    if (!ntl::is_ioctl<query_stats>(code)) {
      out.clear();
      throw ntl::exception(STATUS_INVALID_DEVICE_REQUEST,
                           "unknown flow-monitor IOCTL");
    }
    if (!ntl::ioctl_write_output<query_stats>(out, state->snapshot()))
      throw ntl::exception(STATUS_BUFFER_TOO_SMALL,
                           "flow-monitor output is too small");
  });

  auto callouts = std::make_shared<ntl::wfp::callout_driver<>>(driver);
  g_state = state.get();
  auto target_v4 =
      callouts->add_stream<monitor_flow,
                           observe_stream<wfp_flow_monitor::stream_layer_v4>>(
          wfp_flow_monitor::stream_callout_key_v4);
  if (!target_v4) {
    g_state = nullptr;
    return target_v4.status();
  }
  state->target_v4 = *target_v4;

  auto target_v6 =
      callouts->add_stream<monitor_flow,
                           observe_stream<wfp_flow_monitor::stream_layer_v6>>(
          wfp_flow_monitor::stream_callout_key_v6);
  if (!target_v6) {
    g_state = nullptr;
    (void)callouts->reset();
    return target_v6.status();
  }
  state->target_v6 = *target_v6;

  const ntl::status flow_status_v4 =
      callouts->add<begin_flow<wfp_flow_monitor::flow_layer_v4,
                               wfp_flow_monitor::stream_layer_v4>>(
          wfp_flow_monitor::flow_callout_key_v4);
  if (!flow_status_v4.is_ok()) {
    g_state = nullptr;
    (void)callouts->reset();
    return flow_status_v4;
  }

  const ntl::status flow_status_v6 =
      callouts->add<begin_flow<wfp_flow_monitor::flow_layer_v6,
                               wfp_flow_monitor::stream_layer_v6>>(
          wfp_flow_monitor::flow_callout_key_v6);
  if (!flow_status_v6.is_ok()) {
    g_state = nullptr;
    (void)callouts->reset();
    return flow_status_v6;
  }

  driver.on_unload([state, endpoint, callouts] {
    endpoint->link().reset();
    const ntl::status result = callouts->reset();
    NT_ASSERT(result.is_ok());
    g_state = nullptr;
    endpoint->reset();
  });
  return ntl::status::ok();
}
