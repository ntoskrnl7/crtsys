#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif

#define WIN32_NO_STATUS
#include <windows.h>
#undef WIN32_NO_STATUS
#include <ntstatus.h>
#include <winternl.h>

#include <array>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <stdexcept>
#include <string>
#include <system_error>
#include <vector>

#include <ntl/rpc/client>

#include "rpc_streaming.hpp"
#include "rpc_streaming_macros.hpp"

namespace {

using namespace std::chrono_literals;
constexpr wchar_t endpoint_name[] = L"crtsys_rpc_streaming";
constexpr wchar_t endpoint_path[] =
    L"\\\\?\\Global\\GLOBALROOT\\Device\\crtsys_rpc_streaming";

class raw_stream_session {
public:
  raw_stream_session()
      : handle_(CreateFileW(endpoint_path, GENERIC_READ | GENERIC_WRITE, 0,
                            nullptr, OPEN_EXISTING, 0, nullptr)) {
    if (handle_ == INVALID_HANDLE_VALUE)
      throw std::system_error(static_cast<int>(GetLastError()),
                              std::system_category(),
                              "could not open raw streaming RPC endpoint");

    ntl::rpc::detail::session_control_response response{};
    DWORD returned = 0;
    ntl::rpc::detail::session_control_request create{
        ntl::rpc::detail::session_control_version,
        ntl::rpc::detail::session_control_operation::create,
        {},
        0,
        0,
        0};
    if (!DeviceIoControl(handle_, ntl::rpc::session_control_code(), &create,
                         sizeof(create), &response, sizeof(response), &returned,
                         nullptr) ||
        returned != sizeof(response))
      fail("raw RPC session creation failed");
    session_ = response.session;

    ntl::rpc::detail::session_control_request subscribe{
        ntl::rpc::detail::session_control_version,
        ntl::rpc::detail::session_control_operation::subscribe,
        session_.token,
        crtsys_rpc_streaming::records.id(),
        0,
        0};
    if (!DeviceIoControl(handle_, ntl::rpc::session_control_code(), &subscribe,
                         sizeof(subscribe), nullptr, 0, &returned, nullptr))
      fail("raw RPC stream subscription failed");
  }

  raw_stream_session(const raw_stream_session &) = delete;
  raw_stream_session &operator=(const raw_stream_session &) = delete;

  ~raw_stream_session() { release(); }

  bool rejects(const void *data, DWORD size) const noexcept {
    DWORD returned = 0;
    return DeviceIoControl(handle_,
                           crtsys_rpc_streaming::records.upload_method().code(),
                           const_cast<void *>(data), size, nullptr, 0,
                           &returned, nullptr) == FALSE;
  }

  bool rejects_batch(
      ntl::rpc::detail::reliable_batch_wait_request request) const noexcept {
    std::array<unsigned char, 1024> output{};
    request.max_bytes = static_cast<std::uint32_t>(output.size());
    DWORD returned = 0;
    return DeviceIoControl(handle_, ntl::rpc::notification_control_code(),
                           &request, sizeof(request), output.data(),
                           static_cast<DWORD>(output.size()), &returned,
                           nullptr) == FALSE;
  }

private:
  [[noreturn]] void fail(const char *message) {
    const DWORD error = GetLastError();
    release();
    throw std::system_error(static_cast<int>(error), std::system_category(),
                            message);
  }

  void release() noexcept {
    if (handle_ == INVALID_HANDLE_VALUE)
      return;
    if (session_.token.valid()) {
      ntl::rpc::detail::session_control_request close{
          ntl::rpc::detail::session_control_version,
          ntl::rpc::detail::session_control_operation::close,
          session_.token,
          0,
          0,
          0};
      DWORD returned = 0;
      (void)DeviceIoControl(handle_, ntl::rpc::session_control_code(), &close,
                            sizeof(close), nullptr, 0, &returned, nullptr);
    }
    CloseHandle(handle_);
    handle_ = INVALID_HANDLE_VALUE;
    session_ = {};
  }

  HANDLE handle_ = INVALID_HANDLE_VALUE;
  ntl::rpc::session_info session_{};
};

ntl::rpc::contract_requirements requirements() {
  ntl::rpc::contract_requirements value;
  value.contract_version(crtsys_rpc_streaming::contract_version)
      .transport_features(ntl::rpc::transport_features::current |
                          ntl::rpc::transport_features::asynchronous_calls |
                          ntl::rpc::transport_features::kernel_notifications |
                          ntl::rpc::transport_features::reliable_notifications |
                          ntl::rpc::transport_features::bounded_batches |
                          ntl::rpc::transport_features::priority_delivery |
                          ntl::rpc::transport_features::typed_streams)
      .capabilities(crtsys_rpc_streaming::capabilities::current)
      .method(crtsys_rpc_streaming::records.upload_method())
      .method(crtsys_rpc_streaming::query_stats);
  return value;
}

ntl::rpc::client open_client() {
  ntl::rpc::client client(endpoint_name);
  if (!client)
    throw std::runtime_error("could not open streaming RPC endpoint");
  (void)client.require_contract(requirements());
  return client;
}

rpc_stream_upload upload(rpc_stream_action action, std::uint64_t sequence,
                         std::uint32_t count = 1, std::uint32_t delay_ms = 0) {
  rpc_stream_upload value;
  value.action = action;
  value.sequence = sequence;
  value.count = count;
  value.delay_ms = delay_ms;
  value.text = "stream-" + std::to_string(sequence);
  value.values = {1u, 2u, 3u, 0xFFFFFFFFu};
  return value;
}

void validate_chunk(const ntl::rpc::notification_delivery<
                        ntl::rpc::stream_record<rpc_stream_download>> &delivery,
                    std::uint64_t sequence, std::uint32_t index) {
  if (delivery.sequence() == 0 || !delivery.payload().has_value())
    throw std::runtime_error("stream delivery did not contain a chunk");
  const auto &value = delivery.payload().value();
  if (value.sequence != sequence ||
      value.text != "stream-" + std::to_string(sequence - index) + "-" +
                        std::to_string(index) ||
      value.values !=
          std::vector<std::uint32_t>({1u, 2u, 3u, 0xFFFFFFFFu, index}))
    throw std::runtime_error("stream chunk payload mismatch");
}

void test_bidirectional_and_backpressure() {
  auto client = open_client();
  (void)client.start_session();

  bool unopened_rejected = false;
  try {
    client.write(crtsys_rpc_streaming::records,
                 upload(rpc_stream_action::echo, 99u));
  } catch (const std::system_error &) {
    unopened_rejected = true;
  }
  if (!unopened_rejected)
    throw std::runtime_error("stream upload succeeded before open_stream");

  client.open_stream(crtsys_rpc_streaming::records);

  client.write(crtsys_rpc_streaming::records,
               upload(rpc_stream_action::burst, 100u, 3u));
  const auto after_burst = client.invoke(crtsys_rpc_streaming::query_stats);
  if (after_burst.queued < 2 || after_burst.backpressured != 1)
    throw std::runtime_error("stream queue did not apply bounded backpressure");

  bool full_queue_terminal_rejected = false;
  try {
    client.write(crtsys_rpc_streaming::records,
                 upload(rpc_stream_action::complete, 150u));
  } catch (const std::system_error &) {
    full_queue_terminal_rejected = true;
  }
  if (!full_queue_terminal_rejected)
    throw std::runtime_error("full stream queue accepted a terminal record");

  for (std::uint32_t index = 0; index != 2; ++index) {
    const auto delivery = client.read(crtsys_rpc_streaming::records);
    validate_chunk(delivery, 100u + index, index);
    client.acknowledge(crtsys_rpc_streaming::records, delivery);
  }

  client.write(crtsys_rpc_streaming::records,
               upload(rpc_stream_action::echo, 200u));
  const auto released = client.read(crtsys_rpc_streaming::records);
  validate_chunk(released, 200u, 0);
  client.acknowledge(crtsys_rpc_streaming::records, released);
  client.close_stream(crtsys_rpc_streaming::records);
  client.close_session();
}

void test_timeout_cancel_and_scope_cleanup() {
  auto client = open_client();
  (void)client.start_session();
  client.open_stream(crtsys_rpc_streaming::records);

  auto pending = client.read_async(crtsys_rpc_streaming::records);
  if (pending.wait_for(5ms) != ntl::rpc::async_wait_status::timeout)
    throw std::runtime_error("empty stream read did not time out");
  if (!pending.cancel())
    throw std::runtime_error("stream CancelIoEx found no pending read");
  try {
    (void)pending.get();
    throw std::runtime_error("cancelled stream read returned a record");
  } catch (const std::system_error &error) {
    if (error.code().value() != ERROR_OPERATION_ABORTED)
      throw;
  }

  {
    auto abandoned = client.read_async(crtsys_rpc_streaming::records);
    if (abandoned.wait_for(1ms) != ntl::rpc::async_wait_status::timeout)
      throw std::runtime_error("abandoned stream read completed early");
  }

  auto pending_batch = client.receive_reliable_batch_async(
      crtsys_rpc_streaming::records.download_notification(), 2);
  if (pending_batch.wait_for(5ms) != ntl::rpc::async_wait_status::timeout)
    throw std::runtime_error("empty stream batch read did not time out");
  if (!pending_batch.cancel())
    throw std::runtime_error("stream batch CancelIoEx found no pending read");
  try {
    (void)pending_batch.get();
    throw std::runtime_error("cancelled stream batch read returned records");
  } catch (const std::system_error &error) {
    if (error.code().value() != ERROR_OPERATION_ABORTED)
      throw;
  }
  client.close_stream(crtsys_rpc_streaming::records);
  client.close_session();
}

void test_upload_cancellation() {
  auto client = open_client();
  (void)client.start_session();
  client.open_stream(crtsys_rpc_streaming::records);
  auto pending = client.write_async(
      crtsys_rpc_streaming::records,
      upload(rpc_stream_action::delayed_echo, 300u, 1u, 2000u));
  if (pending.wait_for(10ms) != ntl::rpc::async_wait_status::timeout)
    throw std::runtime_error("delayed stream upload completed too early");
  if (!pending.cancel())
    throw std::runtime_error("stream upload cancellation was not issued");
  try {
    pending.get();
    throw std::runtime_error("cancelled stream upload succeeded");
  } catch (const std::system_error &error) {
    if (error.code().value() != ERROR_OPERATION_ABORTED)
      throw;
  }
  client.close_stream(crtsys_rpc_streaming::records);
  client.close_session();
}

void test_reconnect_replay() {
  ntl::rpc::session_token token{};
  std::uint64_t delivery_sequence = 0;
  {
    auto client = open_client();
    token = client.start_session().token;
    client.open_stream(crtsys_rpc_streaming::records);
    client.write(crtsys_rpc_streaming::records,
                 upload(rpc_stream_action::echo, 400u));
    const auto delivery = client.read(crtsys_rpc_streaming::records);
    validate_chunk(delivery, 400u, 0);
    delivery_sequence = delivery.sequence();
    // Deliberately omit ACK. Handle cleanup must retain this record.
  }

  auto resumed = open_client();
  (void)resumed.resume_session(token);
  resumed.open_stream(crtsys_rpc_streaming::records);
  const auto replay = resumed.read(crtsys_rpc_streaming::records);
  if (replay.sequence() != delivery_sequence)
    throw std::runtime_error("stream reconnect did not replay the same record");
  validate_chunk(replay, 400u, 0);
  resumed.acknowledge(crtsys_rpc_streaming::records, replay);
  resumed.close_stream(crtsys_rpc_streaming::records);
  resumed.close_session();
}

void test_terminal_records() {
  {
    auto client = open_client();
    (void)client.start_session();
    client.open_stream(crtsys_rpc_streaming::records);
    client.write(crtsys_rpc_streaming::records,
                 upload(rpc_stream_action::complete, 500u));

    bool upload_after_terminal_rejected = false;
    try {
      client.write(crtsys_rpc_streaming::records,
                   upload(rpc_stream_action::echo, 501u));
    } catch (const std::system_error &) {
      upload_after_terminal_rejected = true;
    }
    if (!upload_after_terminal_rejected)
      throw std::runtime_error("stream accepted an upload after completion");

    bool duplicate_terminal_rejected = false;
    try {
      client.write(crtsys_rpc_streaming::records,
                   upload(rpc_stream_action::complete, 502u));
    } catch (const std::system_error &) {
      duplicate_terminal_rejected = true;
    }
    if (!duplicate_terminal_rejected)
      throw std::runtime_error("stream accepted a duplicate terminal record");

    const auto completed = client.read(crtsys_rpc_streaming::records);
    if (!completed.payload().is_completed())
      throw std::runtime_error("stream completion record was not observed");
    client.acknowledge(crtsys_rpc_streaming::records, completed);

    bool read_after_terminal_rejected = false;
    try {
      (void)client.read(crtsys_rpc_streaming::records);
    } catch (const std::system_error &error) {
      if (error.code().value() != ERROR_HANDLE_EOF)
        throw;
      read_after_terminal_rejected = true;
    }
    if (!read_after_terminal_rejected)
      throw std::runtime_error("stream read waited after terminal ACK");

    client.close_stream(crtsys_rpc_streaming::records);

    client.open_stream(crtsys_rpc_streaming::records);
    client.write(crtsys_rpc_streaming::records,
                 upload(rpc_stream_action::echo, 503u));
    const auto reopened = client.read(crtsys_rpc_streaming::records);
    validate_chunk(reopened, 503u, 0);
    client.acknowledge(crtsys_rpc_streaming::records, reopened);
    client.close_stream(crtsys_rpc_streaming::records);
    client.close_session();
  }

  auto client = open_client();
  (void)client.start_session();
  client.open_stream(crtsys_rpc_streaming::records);
  client.write(crtsys_rpc_streaming::records,
               upload(rpc_stream_action::fail, 501u));
  const auto failed = client.read(crtsys_rpc_streaming::records);
  if (!failed.payload().is_failed() ||
      failed.payload().status() != STATUS_IO_DEVICE_ERROR)
    throw std::runtime_error("stream failure record lost its NTSTATUS");
  client.acknowledge(crtsys_rpc_streaming::records, failed);
  client.close_stream(crtsys_rpc_streaming::records);
  client.close_session();
}

void test_terminal_reconnect_replay() {
  ntl::rpc::session_token token{};
  std::uint64_t terminal_sequence = 0;
  {
    auto client = open_client();
    token = client.start_session().token;
    client.open_stream(crtsys_rpc_streaming::records);
    client.write(crtsys_rpc_streaming::records,
                 upload(rpc_stream_action::complete, 600u));
    const auto terminal = client.read(crtsys_rpc_streaming::records);
    if (!terminal.payload().is_completed())
      throw std::runtime_error("reconnect fixture did not receive terminal");
    terminal_sequence = terminal.sequence();
    // Keep the terminal unacknowledged so reconnect must replay it and retain
    // the non-writable stream phase.
  }

  auto resumed = open_client();
  (void)resumed.resume_session(token);
  resumed.open_stream(crtsys_rpc_streaming::records);
  bool upload_rejected = false;
  try {
    resumed.write(crtsys_rpc_streaming::records,
                  upload(rpc_stream_action::echo, 601u));
  } catch (const std::system_error &) {
    upload_rejected = true;
  }
  if (!upload_rejected)
    throw std::runtime_error("reconnected terminal stream accepted upload");

  const auto replay = resumed.read(crtsys_rpc_streaming::records);
  if (replay.sequence() != terminal_sequence ||
      !replay.payload().is_completed())
    throw std::runtime_error("terminal reconnect replay changed its record");
  resumed.acknowledge(crtsys_rpc_streaming::records, replay);
  resumed.close_stream(crtsys_rpc_streaming::records);
  resumed.close_session();
}

void test_persisted_restore_order() {
  ntl::rpc::session_token token{};
  {
    auto client = open_client();
    token = client.start_session().token;
    client.open_stream(crtsys_rpc_streaming::records);
    client.write(crtsys_rpc_streaming::records,
                 upload(rpc_stream_action::echo, 700u));
    client.write(crtsys_rpc_streaming::records,
                 upload(rpc_stream_action::complete, 701u));
    // Leave both records unacknowledged. The test store deliberately restores
    // them in reverse order after the in-memory session expires.
  }

  ::Sleep(350);
  auto restored = open_client();
  (void)restored.resume_session(token);
  restored.open_stream(crtsys_rpc_streaming::records);

  const auto chunk = restored.read(crtsys_rpc_streaming::records);
  validate_chunk(chunk, 700u, 0);
  restored.acknowledge(crtsys_rpc_streaming::records, chunk);

  const auto terminal = restored.read(crtsys_rpc_streaming::records);
  if (!terminal.payload().is_completed())
    throw std::runtime_error(
        "persisted stream restore did not preserve terminal ordering");
  restored.acknowledge(crtsys_rpc_streaming::records, terminal);
  restored.close_stream(crtsys_rpc_streaming::records);
  restored.close_session();
}

void test_bounded_stream_batches() {
  auto client = open_client();
  (void)client.start_session();
  client.open_stream(crtsys_rpc_streaming::records);

  std::vector<rpc_stream_upload> uploads;
  bool empty_rejected = false;
  try {
    client.write_batch(crtsys_rpc_streaming::records, uploads);
  } catch (const std::invalid_argument &) {
    empty_rejected = true;
  }
  if (!empty_rejected)
    throw std::runtime_error("empty stream upload batch was accepted");

  bool read_limit_rejected = false;
  try {
    (void)client.receive_reliable_batch_async(
        crtsys_rpc_streaming::records.download_notification(),
        static_cast<std::uint32_t>(
            crtsys_rpc_streaming::records.max_batch_records() + 1));
  } catch (const std::invalid_argument &) {
    read_limit_rejected = true;
  }
  if (!read_limit_rejected)
    throw std::runtime_error("oversized stream batch limit was accepted");

  uploads.push_back(upload(rpc_stream_action::echo, 800u));
  uploads.push_back(upload(rpc_stream_action::echo, 801u));
  client.write_batch(crtsys_rpc_streaming::records, uploads);

  const auto downloads = client.receive_reliable_batch(
      crtsys_rpc_streaming::records.download_notification(), 2);
  if (downloads.size() != 2)
    throw std::runtime_error("stream download batch did not fill its bound");
  validate_chunk(downloads[0], 800u, 0);
  validate_chunk(downloads[1], 801u, 0);
  for (const auto &delivery : downloads.values())
    client.acknowledge(crtsys_rpc_streaming::records.download_notification(),
                       delivery);

  uploads.clear();
  uploads.push_back(upload(rpc_stream_action::echo, 810u));
  uploads.push_back(upload(rpc_stream_action::complete, 811u));
  client.write_batch(crtsys_rpc_streaming::records, uploads);
  const auto terminal_batch = client.receive_reliable_batch(
      crtsys_rpc_streaming::records.download_notification(), 2);
  if (terminal_batch.size() != 2)
    throw std::runtime_error("terminal stream batch record count mismatch");
  validate_chunk(terminal_batch[0], 810u, 0);
  if (!terminal_batch[1].payload().is_completed())
    throw std::runtime_error("terminal stream batch lost completion record");
  for (const auto &delivery : terminal_batch.values())
    client.acknowledge(crtsys_rpc_streaming::records.download_notification(),
                       delivery);

  client.close_stream(crtsys_rpc_streaming::records);
  client.close_session();
}

void test_cross_channel_priority_batch() {
  auto client = open_client();
  (void)client.start_session();
  client.open_stream(crtsys_rpc_streaming::records);
  client.subscribe(crtsys_rpc_streaming::background_events);
  client.subscribe(crtsys_rpc_streaming::critical_events);

  client.write(crtsys_rpc_streaming::records,
               upload(rpc_stream_action::publish_priorities, 900u));
  const auto batch = client.receive_reliable_batch(2, 16 * 1024);
  if (batch.size() != 2 ||
      batch[0].id() != crtsys_rpc_streaming::critical_events.id() ||
      batch[1].id() != crtsys_rpc_streaming::background_events.id())
    throw std::runtime_error(
        "reliable batch did not honor cross-channel priority");
  if (batch[0].decode(crtsys_rpc_streaming::critical_events).payload().value !=
          2 ||
      batch[1].decode(crtsys_rpc_streaming::background_events)
              .payload()
              .value != 1)
    throw std::runtime_error("priority batch payload mismatch");

  client.acknowledge(batch);
  client.unsubscribe(crtsys_rpc_streaming::critical_events);
  client.unsubscribe(crtsys_rpc_streaming::background_events);
  client.close_stream(crtsys_rpc_streaming::records);
  client.close_session();
}

void test_malformed_uploads() {
  const auto before =
      open_client().invoke(crtsys_rpc_streaming::query_stats).uploads;
  raw_stream_session session;
  const std::uint8_t truncated = 0xFFu;
  std::vector<std::uint8_t> oversized(
      crtsys_rpc_streaming::records.upload_size() + 1u);
  if (!session.rejects(&truncated, sizeof(truncated)) ||
      !session.rejects(oversized.data(), static_cast<DWORD>(oversized.size())))
    throw std::runtime_error("stream accepted malformed upload bytes");

  const ntl::rpc::detail::reliable_batch_wait_request invalid_count{
      crtsys_rpc_streaming::records.id(),
      static_cast<std::uint32_t>(ntl::rpc::maximum_batch_records + 1), 0, 0};
  const ntl::rpc::detail::reliable_batch_wait_request invalid_reserved{
      crtsys_rpc_streaming::records.id(), 1, 0, 1};
  if (!session.rejects_batch(invalid_count) ||
      !session.rejects_batch(invalid_reserved))
    throw std::runtime_error("stream accepted malformed batch request");

  const auto after =
      open_client().invoke(crtsys_rpc_streaming::query_stats).uploads;
  if (after != before)
    throw std::runtime_error("rejected stream upload executed its callback");
}

template <typename Callback>
void run_test(const char *name, Callback &&callback) {
  std::printf("[RPC streaming] RUN  %s\n", name);
  callback();
  std::printf("[RPC streaming] PASS %s\n", name);
}

} // namespace

int main() {
  std::setvbuf(stdout, nullptr, _IONBF, 0);
  std::setvbuf(stderr, nullptr, _IONBF, 0);
  try {
    run_test("bidirectional and backpressure",
             test_bidirectional_and_backpressure);
    run_test("timeout cancel and scope cleanup",
             test_timeout_cancel_and_scope_cleanup);
    run_test("upload cancellation", test_upload_cancellation);
    run_test("reconnect replay", test_reconnect_replay);
    run_test("terminal records", test_terminal_records);
    run_test("terminal reconnect replay", test_terminal_reconnect_replay);
    run_test("persisted restore order", test_persisted_restore_order);
    run_test("bounded stream batches", test_bounded_stream_batches);
    run_test("cross-channel priority batch", test_cross_channel_priority_batch);
    run_test("malformed uploads", test_malformed_uploads);
    const auto stats = open_client().invoke(crtsys_rpc_streaming::query_stats);
    if (stats.uploads < 6 || stats.queued < 6 || stats.backpressured == 0 ||
        stats.cancellations == 0)
      throw std::runtime_error("stream counters were not exercised");
    std::printf("RPC streaming PASS: uploads=%lu queued=%lu backpressure=%lu "
                "cancellations=%lu cross_bitness_safe=1\n",
                static_cast<unsigned long>(stats.uploads),
                static_cast<unsigned long>(stats.queued),
                static_cast<unsigned long>(stats.backpressured),
                static_cast<unsigned long>(stats.cancellations));
    return 0;
  } catch (const std::exception &error) {
    std::fprintf(stderr, "RPC streaming failure: %s\n", error.what());
    return 1;
  }
}
