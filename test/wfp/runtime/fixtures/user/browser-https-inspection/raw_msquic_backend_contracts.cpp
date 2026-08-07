#include <msquic.h>

#include <atomic>
#include <array>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <memory>
#include <mutex>
#include <span>
#include <stdexcept>
#include <string_view>
#include <thread>
#include <type_traits>
#include <utility>
#include <vector>

#include <ntl/net/http3/msquic_backend>

namespace {

using backend_connection =
    ntl::net::http3::msquic_backend::connection;

void require(bool value, const char *message) {
  if (!value)
    throw std::runtime_error(message);
}

class contract_sink : public ntl::net::quic::backend_sink {
public:
  ntl::status on_connected(std::string_view) noexcept override {
    return ntl::status::ok();
  }
  ntl::status on_request_stream(
      std::uint64_t, ntl::net::scatter_view,
      bool) noexcept override {
    return ntl::status::ok();
  }
  ntl::status on_qpack_encoder_stream(
      ntl::net::scatter_view) noexcept override {
    return ntl::status::ok();
  }
  ntl::status on_peer_bidirectional_stream(
      std::uint64_t, ntl::net::scatter_view,
      bool) noexcept override {
    ++bidirectional;
    return ntl::status::ok();
  }
  ntl::status on_peer_unidirectional_stream(
      std::uint64_t, ntl::net::scatter_view,
      bool) noexcept override {
    ++unidirectional;
    return ntl::status::ok();
  }
  ntl::status on_datagram(
      ntl::net::scatter_view) noexcept override {
    ++datagrams;
    return ntl::status::ok();
  }
  void on_closed(NTSTATUS status) noexcept override {
    std::unique_lock lock(close_lock);
    close_status = status;
    close_entered = true;
    close_changed.notify_all();
    close_changed.wait(lock, [this] {
      return !block_close || release_close;
    });
  }

  void wait_until_close_entered() {
    std::unique_lock lock(close_lock);
    close_changed.wait(lock, [this] { return close_entered; });
  }

  void unblock_close() {
    {
      std::lock_guard guard(close_lock);
      release_close = true;
    }
    close_changed.notify_all();
  }

  NTSTATUS observed_close_status() const noexcept {
    std::lock_guard guard(close_lock);
    return close_status;
  }

  std::size_t bidirectional = 0;
  std::size_t unidirectional = 0;
  std::size_t datagrams = 0;
  bool block_close = false;

private:
  mutable std::mutex close_lock;
  std::condition_variable close_changed;
  bool close_entered = false;
  bool release_close = false;
  NTSTATUS close_status = STATUS_PENDING;
};

class release_during_callback_sink final : public contract_sink {
public:
  void on_closed(NTSTATUS) noexcept override {
    entered.store(true, std::memory_order_release);
    if (owner)
      owner->reset();
    returned.store(true, std::memory_order_release);
  }

  std::shared_ptr<backend_connection> *owner = nullptr;
  std::atomic<bool> entered{false};
  std::atomic<bool> returned{false};
};

struct lifetime_probe {
  explicit lifetime_probe(std::atomic<unsigned> &destroyed) noexcept
      : destroyed_(&destroyed) {}
  ~lifetime_probe() { destroyed_->fetch_add(1, std::memory_order_release); }
  std::atomic<unsigned> *destroyed_;
};

class owned_contract_sink final : public contract_sink {
public:
  explicit owned_contract_sink(std::atomic<unsigned> &destroyed) noexcept
      : destroyed_(&destroyed) {}
  ~owned_contract_sink() override {
    destroyed_->fetch_add(1, std::memory_order_release);
  }

private:
  std::atomic<unsigned> *destroyed_;
};

class fake_msquic {
public:
  enum class handle_kind : std::uint8_t { connection, stream };

  struct handle {
    fake_msquic *owner = nullptr;
    handle_kind kind = handle_kind::connection;
    QUIC_CONNECTION_CALLBACK_HANDLER connection_callback = nullptr;
    QUIC_STREAM_CALLBACK_HANDLER stream_callback = nullptr;
    void *context = nullptr;
    std::uint64_t id = 0;
    std::atomic<bool> shutdown_requested{false};
    std::atomic<bool> shutdown_delivered{false};
    std::atomic<bool> closed{false};
    std::atomic<bool> inside_send{false};
  };

  fake_msquic() {
    connection_.owner = this;
    connection_.kind = handle_kind::connection;
    api_.SetCallbackHandler = &set_callback_handler;
    api_.GetParam = &get_param;
    api_.ConnectionClose = &connection_close;
    api_.ConnectionShutdown = &connection_shutdown;
    api_.ConnectionSetConfiguration = &connection_set_configuration;
    api_.StreamOpen = &stream_open;
    api_.StreamClose = &stream_close;
    api_.StreamStart = &stream_start;
    api_.StreamShutdown = &stream_shutdown;
    api_.StreamSend = &stream_send;
  }

  const QUIC_API_TABLE *api() const noexcept { return &api_; }
  HQUIC connection() noexcept { return as_hquic(connection_); }
  HQUIC configuration() noexcept {
    return reinterpret_cast<HQUIC>(std::uintptr_t{1});
  }

  void fail_configuration_once() noexcept {
    fail_configuration_.store(true, std::memory_order_release);
  }
  void block_stream_send() noexcept {
    std::lock_guard guard(send_lock_);
    block_send_ = true;
  }
  void wait_until_send_entered() {
    std::unique_lock lock(send_lock_);
    send_changed_.wait(lock, [this] { return send_entered_; });
  }
  void release_stream_send() {
    {
      std::lock_guard guard(send_lock_);
      release_send_ = true;
    }
    send_changed_.notify_all();
  }

  std::size_t stream_count() const noexcept {
    std::lock_guard guard(streams_lock_);
    return streams_.size();
  }
  bool stream_closed(std::size_t index) const {
    return stream_at(index).closed.load(std::memory_order_acquire);
  }
  bool closed_during_send() const noexcept {
    return closed_during_send_.load(std::memory_order_acquire);
  }
  std::size_t connection_close_count() const noexcept {
    return connection_close_count_.load(std::memory_order_acquire);
  }
  bool wait_for_connection_close(
      std::size_t expected,
      std::chrono::milliseconds timeout = std::chrono::seconds(2)) const
      noexcept {
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (connection_close_count() != expected) {
      if (std::chrono::steady_clock::now() >= deadline)
        return false;
      std::this_thread::yield();
    }
    return true;
  }
  void *connection_context() const noexcept {
    return connection_.context;
  }
  bool has_connection_callback() const noexcept {
    return connection_.connection_callback != nullptr;
  }

  void emit_stream_shutdown(std::size_t index) {
    auto &stream = stream_at(index);
    if (stream.shutdown_delivered.exchange(
            true, std::memory_order_acq_rel))
      return;
    require(stream.stream_callback != nullptr,
            "stream callback was not installed");
    QUIC_STREAM_EVENT event{};
    event.Type = QUIC_STREAM_EVENT_SHUTDOWN_COMPLETE;
    event.SHUTDOWN_COMPLETE.AppCloseInProgress = FALSE;
    (void)stream.stream_callback(
        as_hquic(stream), stream.context, &event);
  }

  void emit_connection_shutdown() {
    if (connection_.shutdown_delivered.exchange(
            true, std::memory_order_acq_rel))
      return;
    require(connection_.connection_callback != nullptr,
            "connection callback was not installed");
    QUIC_CONNECTION_EVENT event{};
    event.Type = QUIC_CONNECTION_EVENT_SHUTDOWN_COMPLETE;
    event.SHUTDOWN_COMPLETE.AppCloseInProgress = FALSE;
    (void)connection_.connection_callback(
        connection(), connection_.context, &event);
  }

  void emit_transport_failure(QUIC_STATUS status) {
    require(connection_.connection_callback != nullptr,
            "connection callback was not installed");
    QUIC_CONNECTION_EVENT event{};
    event.Type = QUIC_CONNECTION_EVENT_SHUTDOWN_INITIATED_BY_TRANSPORT;
    event.SHUTDOWN_INITIATED_BY_TRANSPORT.Status = status;
    (void)connection_.connection_callback(
        connection(), connection_.context, &event);
  }

  void complete_pending_shutdowns() noexcept {
    {
      std::lock_guard guard(streams_lock_);
      for (const auto &candidate : streams_) {
        auto &stream = *candidate;
        if (!stream.stream_callback ||
            stream.shutdown_delivered.exchange(
                true, std::memory_order_acq_rel))
          continue;
        QUIC_STREAM_EVENT event{};
        event.Type = QUIC_STREAM_EVENT_SHUTDOWN_COMPLETE;
        event.SHUTDOWN_COMPLETE.AppCloseInProgress = FALSE;
        (void)stream.stream_callback(
            as_hquic(stream), stream.context, &event);
      }
    }
    if (!connection_.connection_callback ||
        connection_.shutdown_delivered.exchange(
            true, std::memory_order_acq_rel))
      return;
    QUIC_CONNECTION_EVENT event{};
    event.Type = QUIC_CONNECTION_EVENT_SHUTDOWN_COMPLETE;
    event.SHUTDOWN_COMPLETE.AppCloseInProgress = FALSE;
    (void)connection_.connection_callback(
        connection(), connection_.context, &event);
  }

private:
  static HQUIC as_hquic(handle &value) noexcept {
    return reinterpret_cast<HQUIC>(&value);
  }
  static handle &from_hquic(HQUIC value) noexcept {
    return *reinterpret_cast<handle *>(value);
  }

  handle &stream_at(std::size_t index) const {
    std::lock_guard guard(streams_lock_);
    require(index < streams_.size(), "fake stream index is out of range");
    return *streams_[index];
  }

  static void QUIC_API set_callback_handler(
      HQUIC value, void *callback, void *context) {
    auto &target = from_hquic(value);
    target.context = context;
    if (target.kind == handle_kind::connection)
      target.connection_callback =
          reinterpret_cast<QUIC_CONNECTION_CALLBACK_HANDLER>(callback);
    else
      target.stream_callback =
          reinterpret_cast<QUIC_STREAM_CALLBACK_HANDLER>(callback);
  }

  static QUIC_STATUS QUIC_API get_param(
      HQUIC value, std::uint32_t parameter,
      std::uint32_t *size, void *buffer) {
    auto &target = from_hquic(value);
    if (target.kind != handle_kind::stream ||
        parameter != QUIC_PARAM_STREAM_ID || !size || !buffer ||
        *size < sizeof(target.id))
      return QUIC_STATUS_INVALID_PARAMETER;
    *size = sizeof(target.id);
    std::memcpy(buffer, &target.id, sizeof(target.id));
    return QUIC_STATUS_SUCCESS;
  }

  static void QUIC_API connection_close(HQUIC value) {
    auto &target = from_hquic(value);
    target.closed.store(true, std::memory_order_release);
    target.owner->connection_close_count_.fetch_add(
        1, std::memory_order_acq_rel);
  }

  static void QUIC_API connection_shutdown(
      HQUIC value, QUIC_CONNECTION_SHUTDOWN_FLAGS,
      QUIC_UINT62) {
    from_hquic(value).shutdown_requested.store(
        true, std::memory_order_release);
  }

  static QUIC_STATUS QUIC_API connection_set_configuration(
      HQUIC value, HQUIC) {
    auto &target = from_hquic(value);
    return target.owner->fail_configuration_.exchange(
               false, std::memory_order_acq_rel)
               ? QUIC_STATUS_ABORTED
               : QUIC_STATUS_SUCCESS;
  }

  static QUIC_STATUS QUIC_API stream_open(
      HQUIC value, QUIC_STREAM_OPEN_FLAGS flags,
      QUIC_STREAM_CALLBACK_HANDLER callback,
      void *context, HQUIC *stream) {
    if (!stream)
      return QUIC_STATUS_INVALID_PARAMETER;
    auto &connection = from_hquic(value);
    auto candidate = std::unique_ptr<handle>(
        new (std::nothrow) handle{});
    if (!candidate)
      return QUIC_STATUS_OUT_OF_MEMORY;
    candidate->owner = connection.owner;
    candidate->kind = handle_kind::stream;
    candidate->stream_callback = callback;
    candidate->context = context;
    auto &next_stream_id =
        (flags & QUIC_STREAM_OPEN_FLAG_UNIDIRECTIONAL) != 0
            ? connection.owner->next_unidirectional_stream_id_
            : connection.owner->next_bidirectional_stream_id_;
    candidate->id = next_stream_id.fetch_add(
        4, std::memory_order_acq_rel);
    handle *raw = candidate.get();
    {
      std::lock_guard guard(connection.owner->streams_lock_);
      connection.owner->streams_.push_back(std::move(candidate));
    }
    *stream = as_hquic(*raw);
    return QUIC_STATUS_SUCCESS;
  }

  static void QUIC_API stream_close(HQUIC value) {
    auto &target = from_hquic(value);
    if (target.inside_send.load(std::memory_order_acquire))
      target.owner->closed_during_send_.store(
          true, std::memory_order_release);
    target.closed.store(true, std::memory_order_release);
  }

  static QUIC_STATUS QUIC_API stream_start(
      HQUIC value, QUIC_STREAM_START_FLAGS) {
    auto &target = from_hquic(value);
    QUIC_STREAM_EVENT event{};
    event.Type = QUIC_STREAM_EVENT_START_COMPLETE;
    event.START_COMPLETE.Status = QUIC_STATUS_SUCCESS;
    event.START_COMPLETE.ID = target.id;
    (void)target.stream_callback(
        value, target.context, &event);
    return QUIC_STATUS_SUCCESS;
  }

  static QUIC_STATUS QUIC_API stream_shutdown(
      HQUIC value, QUIC_STREAM_SHUTDOWN_FLAGS,
      QUIC_UINT62) {
    from_hquic(value).shutdown_requested.store(
        true, std::memory_order_release);
    return QUIC_STATUS_SUCCESS;
  }

  static QUIC_STATUS QUIC_API stream_send(
      HQUIC value, const QUIC_BUFFER *const,
      std::uint32_t, QUIC_SEND_FLAGS,
      void *client_context) {
    auto &target = from_hquic(value);
    target.inside_send.store(true, std::memory_order_release);
    {
      std::unique_lock lock(target.owner->send_lock_);
      target.owner->send_entered_ = true;
      target.owner->send_changed_.notify_all();
      target.owner->send_changed_.wait(lock, [&] {
        return !target.owner->block_send_ ||
               target.owner->release_send_;
      });
    }
    QUIC_STREAM_EVENT event{};
    event.Type = QUIC_STREAM_EVENT_SEND_COMPLETE;
    event.SEND_COMPLETE.ClientContext = client_context;
    (void)target.stream_callback(value, target.context, &event);
    target.inside_send.store(false, std::memory_order_release);
    return QUIC_STATUS_SUCCESS;
  }

  QUIC_API_TABLE api_{};
  handle connection_{};
  mutable std::mutex streams_lock_;
  std::vector<std::unique_ptr<handle>> streams_;
  std::atomic<bool> fail_configuration_{false};
  // try_accept() creates a server connection. Its locally initiated
  // bidirectional streams use IDs 1, 5, 9, ... and unidirectional streams
  // use IDs 3, 7, 11, ... .
  std::atomic<std::uint64_t> next_bidirectional_stream_id_{1};
  std::atomic<std::uint64_t> next_unidirectional_stream_id_{3};
  std::atomic<std::size_t> connection_close_count_{0};
  std::atomic<bool> closed_during_send_{false};
  std::mutex send_lock_;
  std::condition_variable send_changed_;
  bool block_send_ = false;
  bool send_entered_ = false;
  bool release_send_ = false;
};

static_assert(!std::is_copy_constructible_v<backend_connection>);
static_assert(
    ntl::net::http3::msquic_backend::capabilities.available);
static_assert(
    ntl::net::http3::msquic_backend::capabilities
        .bidirectional_streams);
static_assert(
    ntl::net::http3::msquic_backend::capabilities
        .unidirectional_streams);
static_assert(
    ntl::net::http3::msquic_backend::capabilities
        .quic_datagrams);
static_assert(
    ntl::net::http3::msquic_backend::capabilities.webtransport ==
    ntl::net::http3::msquic_backend::capabilities.reliable_reset_at);
static_assert(
    !ntl::net::http3::msquic_backend::capabilities
         .arbitrary_browser_server_identity);

ntl::result<std::shared_ptr<backend_connection>> accept(
    fake_msquic &fake, contract_sink &sink,
    std::size_t maximum_streams = 8,
    std::chrono::milliseconds timeout = std::chrono::seconds(2)) {
  QUIC_NEW_CONNECTION_INFO information{};
  auto indication = ntl::net::http3::msquic_backend::
      borrowed_accepted_connection::from_native(
          fake.connection(), information);
  return backend_connection::try_accept_borrowed(
      fake.api(), std::move(indication), fake.configuration(), sink,
      {.maximum_streams = maximum_streams,
       .maximum_receive_indication = 4096,
       .maximum_send_size = 4096,
       .maximum_prefix_bytes = 8,
       .shutdown_timeout = timeout});
}

void finish_connection(
    std::shared_ptr<backend_connection> &connection,
    fake_msquic &fake) {
  connection->stop();
  fake.emit_connection_shutdown();
  require(connection->drain().is_ok(), "connection did not drain");
  connection.reset();
  require(fake.wait_for_connection_close(1),
          "owned connection handle was not closed exactly once");
}

class accepted_connection_cleanup final {
public:
  accepted_connection_cleanup(
      std::shared_ptr<backend_connection> &connection,
      fake_msquic &fake) noexcept
      : connection_(connection), fake_(fake) {}

  accepted_connection_cleanup(
      const accepted_connection_cleanup &) = delete;
  accepted_connection_cleanup &operator=(
      const accepted_connection_cleanup &) = delete;

  ~accepted_connection_cleanup() {
    if (!connection_)
      return;
    connection_->stop();
    fake_.complete_pending_shutdowns();
    connection_.reset();
    (void)fake_.wait_for_connection_close(1);
  }

private:
  std::shared_ptr<backend_connection> &connection_;
  fake_msquic &fake_;
};

void test_rejected_accept_disarms_callback() {
  fake_msquic fake;
  contract_sink sink;
  fake.fail_configuration_once();
  auto rejected = accept(fake, sink);
  require(!rejected, "failed configuration was accepted");
  require(static_cast<NTSTATUS>(rejected.status()) ==
              static_cast<NTSTATUS>(QUIC_STATUS_ABORTED),
          "synchronous MsQuic status was not preserved");
  require(fake.connection_context() == nullptr,
          "rejected connection retained an object callback context");
  require(fake.has_connection_callback(),
          "rejected connection did not install the discard callback");
  fake.emit_connection_shutdown();
  require(fake.connection_close_count() == 0,
          "rejected listener-owned connection was closed by the backend");
}

void test_transport_status_and_first_error_are_preserved() {
  {
    fake_msquic fake;
    contract_sink sink;
    auto accepted = accept(fake, sink);
    require(static_cast<bool>(accepted),
            "status-propagation connection was not accepted");
    fake.emit_transport_failure(QUIC_STATUS_REQUIRED_CERTIFICATE);
    fake.emit_transport_failure(QUIC_STATUS_CONNECTION_REFUSED);
    fake.emit_connection_shutdown();
    const ntl::status terminal = (*accepted)->run_borrowed(sink);
    require(static_cast<NTSTATUS>(terminal) ==
                static_cast<NTSTATUS>(QUIC_STATUS_REQUIRED_CERTIFICATE),
            "transport status or first terminal error was overwritten");
    require(sink.observed_close_status() ==
                static_cast<NTSTATUS>(QUIC_STATUS_REQUIRED_CERTIFICATE),
            "shutdown callback did not receive the terminal snapshot");
    require((*accepted)->drain().is_ok(),
            "status-propagation connection did not drain");
    accepted->reset();
    require(fake.wait_for_connection_close(1),
            "status-propagation connection cleanup was not joined");
  }

  {
    fake_msquic fake;
    contract_sink sink;
    auto accepted = accept(fake, sink);
    require(static_cast<bool>(accepted),
            "concurrent-error connection was not accepted");
    std::atomic<bool> start{false};
    std::thread certificate([&] {
      while (!start.load(std::memory_order_acquire))
        std::this_thread::yield();
      fake.emit_transport_failure(QUIC_STATUS_BAD_CERTIFICATE);
    });
    std::thread unreachable([&] {
      while (!start.load(std::memory_order_acquire))
        std::this_thread::yield();
      fake.emit_transport_failure(QUIC_STATUS_UNREACHABLE);
    });
    start.store(true, std::memory_order_release);
    certificate.join();
    unreachable.join();
    fake.emit_transport_failure(QUIC_STATUS_INTERNAL_ERROR);
    fake.emit_connection_shutdown();
    const NTSTATUS terminal =
        static_cast<NTSTATUS>((*accepted)->run_borrowed(sink));
    require(terminal == static_cast<NTSTATUS>(QUIC_STATUS_BAD_CERTIFICATE) ||
                terminal == static_cast<NTSTATUS>(QUIC_STATUS_UNREACHABLE),
            "concurrent terminal writers did not preserve their first error");
    require(sink.observed_close_status() == terminal,
            "concurrent terminal snapshot disagreed with run()");
    require((*accepted)->drain().is_ok(),
            "concurrent-error connection did not drain");
    accepted->reset();
    require(fake.wait_for_connection_close(1),
            "concurrent-error connection cleanup was not joined");
  }
}

void test_post_start_failures_keep_callback_context() {
  {
    fake_msquic fake;
    contract_sink sink;
    auto accepted = accept(fake, sink, 1);
    require(static_cast<bool>(accepted),
            "quota fixture connection was not accepted");
    accepted_connection_cleanup cleanup(*accepted, fake);
    std::uint64_t first = 0;
    std::uint64_t second = 0;
    require((*accepted)->open_request_stream(first).is_ok(),
            "first stream did not open");
    require(first == 1,
            "server request stream did not reserve stream ID 1");
    require(!(*accepted)->open_request_stream(second).is_ok(),
            "stream quota failure was ignored");
    fake.emit_stream_shutdown(1);
    fake.emit_stream_shutdown(0);
    finish_connection(*accepted, fake);
  }
}

void test_local_stream_ids_follow_server_quic_sequence() {
  fake_msquic fake;
  contract_sink sink;
  auto accepted = accept(fake, sink, 4);
  require(static_cast<bool>(accepted),
          "stream-ID fixture connection was not accepted");
  accepted_connection_cleanup cleanup(*accepted, fake);
  std::uint64_t first = 0;
  std::uint64_t second = 0;
  std::uint64_t unidirectional = 0;
  require((*accepted)->open_request_stream(first).is_ok(),
          "first stream-ID fixture stream did not open");
  require((*accepted)->open_request_stream(second).is_ok(),
          "second stream-ID fixture stream did not open");
  require((*accepted)->open_unidirectional_stream(
              unidirectional).is_ok(),
          "unidirectional stream-ID fixture stream did not open");
  require(first == 1 && second == 5 && unidirectional == 3,
          "server streams did not use bidirectional IDs 1 and 5 and "
          "unidirectional ID 3");
  fake.emit_stream_shutdown(2);
  fake.emit_stream_shutdown(1);
  fake.emit_stream_shutdown(0);
  finish_connection(*accepted, fake);
}

void test_write_rundown_precedes_stream_close() {
  fake_msquic fake;
  contract_sink sink;
  auto accepted = accept(fake, sink);
  require(static_cast<bool>(accepted),
          "write-race connection was not accepted");
  accepted_connection_cleanup cleanup(*accepted, fake);
  std::uint64_t id = 0;
  require((*accepted)->open_request_stream(id).is_ok(),
          "write-race stream did not open");
  fake.block_stream_send();
  constexpr std::array<std::byte, 1> payload{std::byte{'x'}};
  ntl::status write_status = STATUS_UNSUCCESSFUL;
  std::thread writer([&] {
    write_status = (*accepted)->write_stream(
        id, ntl::net::scatter_view::from_contiguous(payload), false);
  });
  fake.wait_until_send_entered();
  fake.emit_stream_shutdown(0);
  require(!fake.stream_closed(0),
          "SHUTDOWN_COMPLETE closed a handle used by StreamSend");
  fake.release_stream_send();
  writer.join();
  require(write_status.is_ok(), "blocked StreamSend failed");
  require(fake.stream_closed(0) && !fake.closed_during_send(),
          "stream handle did not close after the write rundown");
  finish_connection(*accepted, fake);
}

void test_drain_waits_for_callback_exit() {
  fake_msquic fake;
  contract_sink sink;
  sink.block_close = true;
  auto accepted = accept(fake, sink, 8, std::chrono::seconds(2));
  require(static_cast<bool>(accepted),
          "callback-rundown connection was not accepted");
  (*accepted)->stop();
  std::thread callback([&] { fake.emit_connection_shutdown(); });
  sink.wait_until_close_entered();
  std::atomic<bool> drain_finished{false};
  ntl::status drain_status = STATUS_UNSUCCESSFUL;
  std::thread drainer([&] {
    drain_status = (*accepted)->drain();
    drain_finished.store(true, std::memory_order_release);
  });
  std::this_thread::sleep_for(std::chrono::milliseconds(30));
  require(!drain_finished.load(std::memory_order_acquire),
          "drain returned while on_closed was still executing");
  sink.unblock_close();
  callback.join();
  drainer.join();
  require(drain_status.is_ok(), "callback rundown did not drain");
  accepted->reset();
  require(fake.wait_for_connection_close(1),
          "callback-rundown connection was not closed once");
}

void test_last_release_inside_callback_is_deferred() {
  fake_msquic fake;
  release_during_callback_sink sink;
  auto accepted = accept(fake, sink);
  require(static_cast<bool>(accepted),
          "callback-release connection was not accepted");
  auto connection = std::move(*accepted);
  sink.owner = &connection;
  connection->stop();
  fake.emit_connection_shutdown();
  require(sink.entered.load(std::memory_order_acquire) &&
              sink.returned.load(std::memory_order_acquire) && !connection,
          "last owner was not safely released inside on_closed");
  require(fake.wait_for_connection_close(1),
          "callback-released connection was not retired asynchronously");
}

void test_ordinary_factory_owns_sink_and_native_context() {
  fake_msquic fake;
  std::atomic<unsigned> owner_destroyed{0};
  std::atomic<unsigned> sink_destroyed{0};
  auto native_owner = std::make_shared<lifetime_probe>(owner_destroyed);
  auto sink = std::make_shared<owned_contract_sink>(sink_destroyed);
  QUIC_NEW_CONNECTION_INFO information{};
  auto indication = ntl::net::http3::msquic_backend::
      borrowed_accepted_connection::from_native(
          fake.connection(), information);
  auto accepted = backend_connection::try_accept(
      ntl::net::http3::msquic_backend::connection_context::
          retain_borrowed_native(
              fake.api(), reinterpret_cast<HQUIC>(std::uintptr_t{2}),
              fake.configuration(), native_owner),
      std::move(indication), sink,
      {.maximum_streams = 8,
       .maximum_receive_indication = 4096,
       .maximum_send_size = 4096,
       .maximum_prefix_bytes = 8,
       .shutdown_timeout = std::chrono::seconds(2)});
  require(static_cast<bool>(accepted),
          "ordinary owning factory rejected a valid context");
  auto connection = std::move(*accepted);
  native_owner.reset();
  sink.reset();
  require(owner_destroyed.load(std::memory_order_acquire) == 0 &&
              sink_destroyed.load(std::memory_order_acquire) == 0,
          "connection did not retain its native context and sink");
  connection->stop();
  fake.emit_connection_shutdown();
  require(connection->drain().is_ok(),
          "ordinary owning connection did not drain");
  connection.reset();
  require(fake.wait_for_connection_close(1),
          "ordinary owning connection was not retired");
  const auto deadline =
      std::chrono::steady_clock::now() + std::chrono::seconds(2);
  while ((owner_destroyed.load(std::memory_order_acquire) != 1 ||
          sink_destroyed.load(std::memory_order_acquire) != 1) &&
         std::chrono::steady_clock::now() < deadline)
    std::this_thread::yield();
  require(owner_destroyed.load(std::memory_order_acquire) == 1 &&
              sink_destroyed.load(std::memory_order_acquire) == 1,
          "ordinary owning connection retained a child after cleanup");
}

} // namespace

int main() {
  try {
    const auto capabilities =
        ntl::net::http3::msquic_backend::capabilities;
    require(capabilities.extended_connect && capabilities.quic_datagrams &&
                capabilities.webtransport ==
                    capabilities.reliable_reset_at,
            "raw MsQuic capability contract changed");
    test_rejected_accept_disarms_callback();
    test_transport_status_and_first_error_are_preserved();
    test_post_start_failures_keep_callback_context();
    test_local_stream_ids_follow_server_quic_sequence();
    test_write_rundown_precedes_stream_close();
    test_drain_waits_for_callback_exit();
    test_last_release_inside_callback_is_deferred();
    test_ordinary_factory_owns_sink_and_native_context();
    std::cout
        << "raw-msquic-backend contracts passed: "
           "extended-connect=pass datagram=pass "
           "preview-capabilities=consistent rejected-accept=disarmed "
           "post-start-lifetime=held write-rundown=ordered "
           "callback-drain=ordered callback-release=deferred owning=retained "
           "status=preserved "
           "terminal-first-error=preserved\n";
    return 0;
  } catch (...) {
    return 1;
  }
}
