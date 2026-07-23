#include "communication_advanced.hpp"
#include "coroutine_runtime.hpp"
#include "runtime_test.hpp"

#include <Windows.h>
#include <fltUser.h>

#include <ntl/flt/communication_client>
#include <ntl/ipc/shared_ring>

#include <cstring>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace {

class secondary_instance_attachment {
public:
  explicit secondary_instance_attachment(std::wstring volume)
      : volume_(std::move(volume)) {
    using namespace crtsys_flt_runtime_test;

    const HRESULT detached =
        FilterDetach(filter_name, volume_.c_str(), secondary_instance_name);
    if (FAILED(detached) && detached != ERROR_FLT_INSTANCE_NOT_FOUND) {
      status_ = detached;
      return;
    }

    status_ = FilterAttachAtAltitude(
        filter_name, volume_.c_str(), secondary_altitude,
        secondary_instance_name, 0, nullptr);
    attached_ = SUCCEEDED(status_);
  }

  secondary_instance_attachment(const secondary_instance_attachment &) = delete;
  secondary_instance_attachment &
  operator=(const secondary_instance_attachment &) = delete;

  ~secondary_instance_attachment() { (void)detach(); }

  HRESULT status() const noexcept { return status_; }

  HRESULT detach() noexcept {
    if (!attached_)
      return S_OK;

    using namespace crtsys_flt_runtime_test;
    const HRESULT result =
        FilterDetach(filter_name, volume_.c_str(), secondary_instance_name);
    if (SUCCEEDED(result))
      attached_ = false;
    return result;
  }

private:
  std::wstring volume_;
  HRESULT status_ = E_UNEXPECTED;
  bool attached_ = false;
};

} // namespace

int wmain(int argc, wchar_t **argv) {
  namespace fs = std::filesystem;
  using namespace crtsys_flt_runtime_test;

  if (argc == 4 && std::wstring_view(argv[1]) == L"--security-probe") {
    const bool expect_allowed = std::wstring_view(argv[3]) == L"allow";
    const bool expect_denied = std::wstring_view(argv[3]) == L"deny";
    if (!expect_allowed && !expect_denied) {
      std::cerr << "security probe expectation must be 'allow' or 'deny'\n";
      return 2;
    }
    try {
      auto probe = ntl::flt::communication_client::connect(argv[2]);
      (void)probe;
      if (expect_denied) {
        std::cerr << "NTL minifilter security probe was unexpectedly allowed\n";
        return 1;
      }
      std::cout << "NTL minifilter security probe PASS (allowed)\n";
      return 0;
    } catch (const std::exception &failure) {
      if (expect_denied) {
        std::cout << "NTL minifilter security probe PASS (denied)\n";
        return 0;
      }
      std::cerr << "NTL minifilter security probe rejected: "
                << failure.what() << '\n';
      return 1;
    }
  }

  const fs::path root = fs::temp_directory_path();
  const fs::path source = root / file_name;
  const fs::path renamed = root / renamed_file_name;

  secondary_instance_attachment secondary{root.root_name().wstring()};
  if (FAILED(secondary.status())) {
    std::cerr << "failed to attach secondary minifilter instance: 0x"
              << std::hex << std::uppercase
              << static_cast<unsigned long>(secondary.status()) << '\n';
    return 1;
  }

  const char *communication_stage = "connect";
  try {
    auto port = connect();
    communication_stage = "query contract";
    const auto contract = port.query_contract();
    constexpr std::uint64_t required_transport =
        ntl::rpc::transport_features::resource_limits |
        ntl::rpc::transport_features::secure_endpoint |
        ntl::rpc::transport_features::asynchronous_calls |
        ntl::rpc::transport_features::kernel_notifications |
        ntl::rpc::transport_features::caller_security_context |
        ntl::rpc::transport_features::client_sessions |
        ntl::rpc::transport_features::reliable_notifications |
        ntl::rpc::transport_features::typed_streams |
        ntl::rpc::transport_features::bounded_batches |
        ntl::rpc::transport_features::priority_delivery |
        ntl::rpc::transport_features::shared_memory_regions;
    if (contract.contract_version() != contract_version ||
        (contract.transport_feature_mask() & required_transport) !=
            required_transport ||
        !contract.supports(ping_method) || !contract.supports(denied_method)) {
      std::cerr << "minifilter contract discovery mismatch\n";
      return 1;
    }
    bool wrong_schema_rejected = false;
    try {
      constexpr ntl::rpc::method<0xA50, std::uint64_t(std::uint32_t)>
          incompatible_ping{};
      port.require_method(incompatible_ping);
    } catch (const std::runtime_error &) {
      wrong_schema_rejected = true;
    }
    if (!wrong_schema_rejected) {
      std::cerr << "minifilter schema hash mismatch was not rejected\n";
      return 1;
    }
    communication_stage = "first typed call";
    if (ping(port, std::uint32_t{41}) != 42) {
      std::cerr << "typed minifilter communication returned a bad result\n";
      return 1;
    }

    bool connect_rejected = false;
    try {
      auto rejected =
          ntl::flt::communication_client::connect(reject_port_name);
      (void)rejected;
    } catch (const ntl::flt::communication_error &) {
      connect_rejected = true;
    }
    if (!connect_rejected) {
      std::cerr << "minifilter on_connect rejection was not enforced\n";
      return 1;
    }

    {
      auto allowed = ntl::flt::communication_client::connect(
          security_allow_port_name);
      (void)allowed;

      bool denied = false;
      try {
        auto rejected = ntl::flt::communication_client::connect(
            security_deny_port_name);
        (void)rejected;
      } catch (const ntl::flt::communication_error &) {
        denied = true;
      }
      if (!denied) {
        std::cerr << "minifilter port security descriptor was not enforced\n";
        return 1;
      }
    }

    {
      auto first = ntl::flt::communication_client::connect(
          connection_limit_port_name);
      bool second_rejected = false;
      try {
        auto second = ntl::flt::communication_client::connect(
            connection_limit_port_name);
        (void)second;
      } catch (const ntl::flt::communication_error &) {
        second_rejected = true;
      }
      if (!second_rejected) {
        std::cerr << "minifilter connection quota was not enforced\n";
        return 1;
      }
    }

    {
      auto first =
          ntl::flt::communication_client::connect(session_limit_port_name);
      bool second_rejected = false;
      try {
        auto second = ntl::flt::communication_client::connect(
            session_limit_port_name);
        (void)second;
      } catch (const ntl::flt::communication_error &) {
        second_rejected = true;
      }
      if (!second_rejected) {
        std::cerr << "minifilter session quota was not enforced\n";
        return 1;
      }
    }
    communication_stage = "advanced communication";
    if (!run_advanced_communication_tests(port))
      return 1;

    bool authorization_observed = false;
    try {
      (void)denied(port, std::uint32_t{1});
    } catch (const ntl::flt::communication_error &) {
      authorization_observed = true;
    }
    if (!authorization_observed) {
      std::cerr << "minifilter pre-decode authorization was not enforced\n";
      return 1;
    }

    auto transient_progress = progress_async(port);
    if (port.invoke(publish_progress_method, std::uint32_t{7}, false) != 0 ||
        transient_progress.get().value != 7) {
      std::cerr << "transient minifilter notification mismatch\n";
      return 1;
    }

    auto cancelled_progress = progress_async(port);
    if (!cancelled_progress.cancel()) {
      std::cerr << "minifilter notification cancellation was not accepted\n";
      return 1;
    }
    try {
      (void)cancelled_progress.get();
      std::cerr << "cancelled minifilter notification returned a value\n";
      return 1;
    } catch (const ntl::flt::communication_error &) {
    }
    auto after_cancel = progress_async(port);
    if (port.invoke(publish_progress_method, std::uint32_t{9}, false) != 0 ||
        after_cancel.get().value != 9) {
      std::cerr << "cancelled minifilter waiter consumed a later message\n";
      return 1;
    }

    {
      auto abandoned_progress = progress_async(port);
      (void)abandoned_progress;
    }
    auto after_abandon = progress_async(port);
    if (port.invoke(publish_progress_method, std::uint32_t{10}, false) != 0 ||
        after_abandon.get().value != 10) {
      std::cerr << "abandoned minifilter waiter consumed a later message\n";
      return 1;
    }

#if defined(__cpp_lib_jthread) && __cpp_lib_jthread >= 201911L
    std::stop_source notification_stop;
    auto stopped_progress = progress_async(notification_stop.get_token(), port);
    (void)notification_stop.request_stop();
    try {
      (void)stopped_progress.get();
      std::cerr << "stop_token minifilter notification returned a value\n";
      return 1;
    } catch (const ntl::flt::communication_error &) {
    }
#endif

    auto reliable_progress = progress_reliable_async(port);
    const auto published_sequence =
        port.invoke(publish_progress_method, std::uint32_t{8}, true);
    const auto reliable_delivery = reliable_progress.get();
    if (published_sequence == 0 ||
        reliable_delivery.sequence() != published_sequence ||
        reliable_delivery.payload().value != 8) {
      std::cerr << "reliable minifilter notification mismatch\n";
      return 1;
    }
    port.acknowledge(progress_notification, reliable_delivery);

    auto quota_receive = progress_reliable_async(port);
    for (std::uint32_t value = 20; value != 24; ++value) {
      if (port.invoke(publish_progress_method, value, true) == 0) {
        std::cerr << "minifilter reliable queue returned no sequence\n";
        return 1;
      }
    }
    bool reliable_quota_observed = false;
    try {
      (void)port.invoke(publish_progress_method, std::uint32_t{24}, true);
    } catch (const ntl::flt::communication_error &) {
      reliable_quota_observed = true;
    }
    if (!reliable_quota_observed) {
      std::cerr << "minifilter reliable queue quota was not enforced\n";
      return 1;
    }
    auto queued_delivery = quota_receive.get();
    for (std::uint32_t expected = 20; expected != 24; ++expected) {
      if (queued_delivery.payload().value != expected) {
        std::cerr << "minifilter reliable queue order mismatch\n";
        return 1;
      }
      port.acknowledge(progress_notification, queued_delivery);
      if (expected != 23)
        queued_delivery = progress_reliable(port);
    }

    {
      auto stream = numbers(port);
      // Exercise a single-record frame that is already buffered when the
      // client changes to batch mode.
      stream.write(std::uint32_t{10});
      ::Sleep(100);
      const auto transitioned_batch = stream.read_batch();
      if (transitioned_batch.size() != 1 ||
          !transitioned_batch.values()[0].payload().has_value() ||
          transitioned_batch.values()[0].payload().value() != 110) {
        std::cerr << "minifilter single-to-batch transition mismatch\n";
        return 1;
      }
      stream.acknowledge(transitioned_batch);

      stream.write(std::uint32_t{11});
      const auto reply = stream.read();
      if (!reply.payload().has_value() || reply.payload().value() != 111) {
        std::cerr << "minifilter stream reply mismatch\n";
        return 1;
      }
      stream.acknowledge(reply);

      auto batch_wait = stream.read_batch_async();
      stream.write_batch(std::vector<std::uint32_t>{12, 13});
      std::vector<std::uint32_t> batch_values;
      while (batch_values.size() != 2) {
        const auto batch = batch_wait.get();
        for (const auto &delivery : batch.values()) {
          if (!delivery.payload().has_value()) {
            std::cerr
                << "minifilter stream batch contained a terminal record\n";
            return 1;
          }
          batch_values.push_back(delivery.payload().value());
        }
        stream.acknowledge(batch);
        if (batch_values.size() < 2)
          batch_wait = stream.read_batch_async();
      }
      if (batch_values != std::vector<std::uint32_t>{112, 113}) {
        std::cerr << "minifilter stream batch mismatch\n";
        return 1;
      }

      stream.write(std::uint32_t{99});
      const auto final_value = stream.read();
      if (!final_value.payload().has_value() ||
          final_value.payload().value() != 199) {
        std::cerr << "minifilter stream terminal value mismatch\n";
        return 1;
      }
      stream.acknowledge(final_value);
      const auto completed = stream.read();
      if (!completed.payload().is_completed()) {
        std::cerr << "minifilter stream completion mismatch\n";
        return 1;
      }
      stream.acknowledge(completed);
      stream.close();

      auto failed_stream = numbers(port);
      failed_stream.write(std::uint32_t{98});
      const auto failed = failed_stream.read();
      if (!failed.payload().is_failed()) {
        std::cerr << "minifilter stream failure was not delivered\n";
        return 1;
      }
      failed_stream.acknowledge(failed);
      failed_stream.close();
    }

    auto retained_stream = [] {
      auto transient_client = connect();
      return numbers(transient_client);
    }();
    retained_stream.write(std::uint32_t{7});
    const auto retained_reply = retained_stream.read();
    if (!retained_reply.payload().has_value() ||
        retained_reply.payload().value() != 107) {
      std::cerr << "minifilter stream did not retain its connection\n";
      return 1;
    }
    retained_stream.acknowledge(retained_reply);
    retained_stream.close();

    ntl::rpc::session_token reconnect_token{};
    std::uint64_t replay_sequence = 0;
    {
      auto first_connection = connect();
      auto first_receive = progress_reliable_async(first_connection);
      replay_sequence = first_connection.invoke(publish_progress_method,
                                                std::uint32_t{77}, true);
      const auto unacknowledged = first_receive.get();
      if (unacknowledged.sequence() != replay_sequence)
        return 1;
      reconnect_token = first_connection.preserve_session();
    }
    {
      auto resumed = resume(reconnect_token);
      const auto replayed = progress_reliable(resumed);
      if (replayed.sequence() != replay_sequence ||
          replayed.payload().value != 77) {
        std::cerr << "minifilter reliable reconnect replay mismatch\n";
        return 1;
      }
      resumed.acknowledge(progress_notification, replayed);
    }

    std::uint64_t priority_session_id = 0;
    ntl::rpc::session_token priority_token{};
    {
      auto priority_target = connect();
      auto background_subscription =
          background_progress_reliable_async(priority_target);
      auto critical_subscription =
          critical_progress_reliable_async(priority_target);
      priority_session_id = priority_target.session().id;
      priority_token = priority_target.preserve_session();
      (void)background_subscription;
      (void)critical_subscription;
    }
    {
      auto priority_publisher = connect();
      const auto background_sequence = priority_publisher.invoke(
          publish_priority_method, priority_session_id, std::uint32_t{31},
          false);
      const auto critical_sequence = priority_publisher.invoke(
          publish_priority_method, priority_session_id, std::uint32_t{32},
          true);

      auto priority_target = resume(priority_token);
      auto background_receive =
          background_progress_reliable_async(priority_target);
      auto critical_receive = critical_progress_reliable_async(priority_target);
      if (critical_receive.wait_for(std::chrono::seconds(2)) !=
          ntl::flt::communication_wait_status::completed) {
        std::cerr
            << "critical minifilter notification was not selected first\n";
        return 1;
      }
      const auto critical_delivery = critical_receive.get();
      if (critical_delivery.sequence() != critical_sequence ||
          critical_delivery.payload().value != 32 ||
          background_receive.ready()) {
        std::cerr << "minifilter notification priority mismatch\n";
        return 1;
      }
      priority_target.acknowledge(critical_progress_notification,
                                  critical_delivery);

      if (background_receive.wait_for(std::chrono::seconds(2)) !=
          ntl::flt::communication_wait_status::completed) {
        std::cerr << "background minifilter notification was not delivered\n";
        return 1;
      }
      const auto background_delivery = background_receive.get();
      if (background_delivery.sequence() != background_sequence ||
          background_delivery.payload().value != 31) {
        std::cerr << "background minifilter notification mismatch\n";
        return 1;
      }
      priority_target.acknowledge(background_progress_notification,
                                  background_delivery);
    }

    std::vector<ntl::flt::communication_async_call<std::uint32_t>> pending;
    for (std::uint32_t value = 0; value != 8; ++value)
      pending.push_back(ping_async(port, value));
    for (std::size_t value = 0; value != pending.size(); ++value) {
      if (pending[value].get() != static_cast<std::uint32_t>(value + 1)) {
        std::cerr << "concurrent minifilter async result mismatch\n";
        return 1;
      }
    }

    auto detached = [] {
      auto transient_client = connect();
      return ping_async(transient_client, std::uint32_t{73});
    }();
    if (detached.get() != 74) {
      std::cerr << "minifilter async call did not retain its connection\n";
      return 1;
    }

    for (std::uint32_t attempt = 0; attempt != 4; ++attempt) {
      auto abandoned = [] {
        auto transient_client = connect();
        return cancellable_count_async(transient_client, std::uint32_t{250});
      }();
      (void)abandoned;
    }

    std::vector<ntl::flt::communication_async_call<std::uint32_t>> saturated;
    for (std::uint32_t index = 0; index != 8; ++index)
      saturated.push_back(cancellable_count_async(port, std::uint32_t{250}));
    bool quota_observed = false;
    try {
      auto overflow = ping_async(port, std::uint32_t{1});
      (void)overflow;
    } catch (const ntl::flt::communication_error &) {
      quota_observed = true;
    }
    if (!quota_observed) {
      std::cerr << "minifilter async pending quota was not enforced\n";
      return 1;
    }
    for (auto &call : saturated) {
      (void)call.cancel();
      try {
        (void)call.get();
      } catch (const ntl::flt::communication_error &) {
      }
    }

    auto cancelled = cancellable_count_async(port, std::uint32_t{250});
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    if (!cancelled.cancel()) {
      std::cerr << "minifilter async cancellation was not accepted\n";
      return 1;
    }
    bool cancellation_observed = false;
    try {
      (void)cancelled.get();
    } catch (const ntl::flt::communication_error &) {
      cancellation_observed = true;
    }
    if (!cancellation_observed) {
      std::cerr << "minifilter callback did not observe cancellation\n";
      return 1;
    }

#if defined(__cpp_lib_jthread) && __cpp_lib_jthread >= 201911L
    std::stop_source stop;
    auto stopped =
        cancellable_count_async(stop.get_token(), port, std::uint32_t{250});
    (void)stop.request_stop();
    bool stop_observed = false;
    try {
      (void)stopped.get();
    } catch (const ntl::flt::communication_error &) {
      stop_observed = true;
    }
    if (!stop_observed) {
      std::cerr << "minifilter stop_token cancellation was not observed\n";
      return 1;
    }
#endif

    auto region = port.register_shared_region(shared_region_bytes);
    test_ring app_to_driver;
    test_ring driver_to_app;
    auto *const base = static_cast<unsigned char *>(region.data());
    if (test_ring::initialize(base, test_ring::required_bytes(), app_to_driver,
                              1) != ntl::ipc::validation_status::success ||
        test_ring::initialize(base + ring_stride, test_ring::required_bytes(),
                              driver_to_app,
                              2) != ntl::ipc::validation_status::success) {
      std::cerr << "failed to initialize minifilter shared rings\n";
      return 1;
    }

    for (std::uint32_t index = 0; index != 3; ++index) {
      if (!app_to_driver.try_write(ring_record{index, index + 10})) {
        std::cerr << "failed to enqueue minifilter shared record\n";
        return 1;
      }
    }

    const auto input_token = region.token(0, test_ring::required_bytes());
    const auto output_token =
        region.token(ring_stride, test_ring::required_bytes());
    if (port.invoke(shared_ring_method, input_token, output_token) != 3) {
      std::cerr << "minifilter shared-ring method processed a bad count\n";
      return 1;
    }

    for (std::uint32_t index = 0; index != 3; ++index) {
      ring_record record{};
      if (!driver_to_app.try_read(record) || record.sequence != index ||
          record.value != index + 1010) {
        std::cerr << "minifilter shared-ring response mismatch\n";
        return 1;
      }
    }

    const auto out_of_range_token = ntl::ipc::buffer_token{
        region.handle(), static_cast<std::uint64_t>(shared_region_bytes - 1), 2};
    bool range_rejected = false;
    try {
      (void)port.invoke(shared_ring_method, out_of_range_token, output_token);
    } catch (const ntl::flt::communication_error &) {
      range_rejected = true;
    }
    if (!range_rejected) {
      std::cerr << "minifilter shared-region range was not rejected\n";
      return 1;
    }

    auto read_only_region = port.register_shared_region(
        shared_region_bytes, ntl::ipc::region_access::driver_read);
    const auto read_only_token =
        read_only_region.token(0, test_ring::required_bytes());
    bool access_rejected = false;
    try {
      (void)port.invoke(shared_ring_method, read_only_token,
                        read_only_token);
    } catch (const ntl::flt::communication_error &) {
      access_rejected = true;
    }
    if (!access_rejected) {
      std::cerr << "minifilter shared-region access was not rejected\n";
      return 1;
    }

    {
      auto pool = region.make_buffer_pool(16);
      auto first_result = pool.try_acquire(32);
      auto second_result = pool.try_acquire(48);
      if (!first_result || !second_result ||
          first_result->token().region != region.handle() ||
          second_result->token().region != region.handle()) {
        std::cerr << "minifilter shared-buffer pool allocation failed\n";
        return 1;
      }
      auto first = std::move(*first_result);
      auto second = std::move(*second_result);
      const auto first_offset = first.token().offset;
      first.release();
      auto reused_result = pool.try_acquire(16);
      if (!reused_result || reused_result->token().offset != first_offset) {
        std::cerr << "minifilter shared-buffer lease reuse failed\n";
        return 1;
      }
      second.release();
      reused_result->release();
      auto whole_result = pool.try_acquire(shared_region_bytes);
      if (!whole_result) {
        std::cerr << "minifilter shared-buffer lease coalescing failed\n";
        return 1;
      }
    }

    bool region_quota_rejected = false;
    try {
      auto third_region = port.register_shared_region(1);
      third_region.close();
    } catch (const ntl::flt::communication_error &) {
      region_quota_rejected = true;
    }
    if (!region_quota_rejected) {
      std::cerr << "minifilter shared-region quota was not enforced\n";
      return 1;
    }
    read_only_region.close();
    region.close();

    bool stale_token_rejected = false;
    try {
      (void)port.invoke(shared_ring_method, input_token, output_token);
    } catch (const ntl::flt::communication_error &) {
      stale_token_rejected = true;
    }
    if (!stale_token_rejected || ping(port, std::uint32_t{9}) != 10) {
      std::cerr << "minifilter stale shared-region token was not isolated\n";
      return 1;
    }

    const auto send_raw =
        [&](const ntl::flt::detail::communication_request_header &header,
            const std::vector<unsigned char> &payload) {
          std::vector<unsigned char> frame(sizeof(header) + payload.size());
          std::memcpy(frame.data(), &header, sizeof(header));
          if (!payload.empty())
            std::memcpy(frame.data() + sizeof(header), payload.data(),
                        payload.size());
          DWORD returned = 0;
          return FilterSendMessage(
              port.native_handle(), frame.data(),
              static_cast<DWORD>(frame.size()), nullptr, 0, &returned);
        };
    auto malformed = ntl::flt::detail::communication_request_header{};
    malformed.payload_size = 1;
    if (SUCCEEDED(send_raw(malformed, {}))) {
      std::cerr << "minifilter truncated request was accepted\n";
      return 1;
    }

    auto bad_magic = ntl::flt::detail::communication_request_header{};
    bad_magic.magic = 0;
    if (SUCCEEDED(send_raw(bad_magic, {}))) {
      std::cerr << "minifilter bad magic was accepted\n";
      return 1;
    }

    auto bad_version = ntl::flt::detail::communication_request_header{};
    bad_version.version =
        static_cast<std::uint16_t>(ntl::flt::detail::communication_version + 1);
    if (SUCCEEDED(send_raw(bad_version, {}))) {
      std::cerr << "minifilter bad protocol version was accepted\n";
      return 1;
    }

    auto bad_operation = ntl::flt::detail::communication_request_header{};
    bad_operation.operation =
        static_cast<ntl::flt::detail::communication_operation>(0xffff);
    if (SUCCEEDED(send_raw(bad_operation, {}))) {
      std::cerr << "minifilter bad operation was accepted\n";
      return 1;
    }

    auto trailing = ntl::flt::detail::communication_request_header{};
    if (SUCCEEDED(send_raw(trailing, std::vector<unsigned char>{0}))) {
      std::cerr << "minifilter trailing request bytes were accepted\n";
      return 1;
    }

    auto oversized = ntl::flt::detail::communication_request_header{};
    oversized.payload_size = static_cast<std::uint32_t>(
        ntl::flt::detail::communication_inbound_payload_limit + 1);
    std::vector<unsigned char> oversized_payload(
        ntl::flt::detail::communication_inbound_payload_limit + 1);
    if (SUCCEEDED(send_raw(oversized, oversized_payload))) {
      std::cerr << "minifilter oversized request was accepted\n";
      return 1;
    }

    if (ping(port, std::uint32_t{19}) != 20) {
      std::cerr << "minifilter malformed input damaged the connection\n";
      return 1;
    }

    communication_stage = "coroutine communication";
    auto coroutine_client = connect();
    if (!run_coroutine_runtime_tests(coroutine_client)) {
      std::cerr << "minifilter coroutine communication failed\n";
      return 1;
    }
  } catch (const ntl::flt::communication_error &failure) {
    std::cerr << "minifilter communication failed during "
              << communication_stage << ": 0x" << std::hex << std::uppercase
              << static_cast<unsigned long>(failure.result()) << '\n';
    return 1;
  } catch (const std::exception &failure) {
    std::cerr << "minifilter communication failed during "
              << communication_stage << ": " << failure.what() << '\n';
    return 1;
  }

  std::error_code error;
  fs::remove(source, error);
  error.clear();
  fs::remove(renamed, error);

  {
    std::ofstream stream(source, std::ios::binary | std::ios::trunc);
    if (!stream) {
      std::cerr << "failed to create " << source << '\n';
      return 1;
    }
    stream << "crtsys NTL minifilter runtime test\n";
  }

  {
    std::ifstream stream(source, std::ios::binary);
    std::string contents;
    if (!stream || !std::getline(stream, contents) ||
        contents != "crtsys NTL minifilter runtime test") {
      std::cerr << "failed to read " << source << '\n';
      fs::remove(source, error);
      return 1;
    }
  }

  fs::rename(source, renamed, error);
  if (error) {
    std::cerr << "rename failed: " << error.message() << '\n';
    fs::remove(source, error);
    return 1;
  }

  fs::remove(renamed, error);
  if (error) {
    std::cerr << "remove failed: " << error.message() << '\n';
    return 1;
  }

  const HRESULT detached = secondary.detach();
  if (FAILED(detached)) {
    std::cerr << "failed to detach secondary minifilter instance: 0x"
              << std::hex << std::uppercase
              << static_cast<unsigned long>(detached) << '\n';
    return 1;
  }

  std::cout << "NTL minifilter runtime test PASS\n";
  return 0;
}
