#include <ntddk.h>

#include <array>
#include <atomic>
#include <chrono>
#include <coroutine>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>

#include <ntl/device_endpoint>
#include <ntl/driver>
#include <ntl/except>
#include <ntl/ioctl>
#include <ntl/net/buffer/owned_bytes>
#include <ntl/net/io/async_transport_stream>
#include <ntl/net/kernel/bidirectional_join>
#include <ntl/net/kernel/waitable_task>
#include <ntl/net/kernel/wsk_redirect>
#include <ntl/net/kernel/wsk_transport>
#include <ntl/system_thread>
#include <ntl/wfp/all>

#include "connect_redirect_contract.hpp"

namespace {

namespace contract = wfp_kernel_connect_redirect;
using query_proxy = ntl::ioctl_from_contract<contract::query_proxy_contract>;
using ntl::net::kernel::waitable_status_task;

ntl::net::kernel::bidirectional_status_task relay(
    ntl::net::io::async_transport_stream &source,
    ntl::net::io::async_transport_stream &destination,
    const std::atomic<bool> &stopping, std::uint64_t &transferred) {
  std::array<std::byte, 16 * 1024> buffer{};
  for (;;) {
    auto received = co_await source.read_some_borrowed(
        buffer, {.timeout = std::chrono::milliseconds(250)});
    if (!received) {
      const NTSTATUS status = static_cast<NTSTATUS>(received.status());
      if (status == STATUS_IO_TIMEOUT &&
          !stopping.load(std::memory_order_acquire))
        continue;
      if (status == STATUS_END_OF_FILE) {
        const auto closed = co_await destination.shutdown_write();
        co_return closed.status.is_ok() && closed.transferred == 0
                      ? ntl::status::ok()
                      : closed.status;
      }
      co_return stopping.load(std::memory_order_acquire)
                    ? ntl::status{STATUS_CANCELLED}
                    : received.status();
    }
    if (*received == 0)
      co_return ntl::status{STATUS_DATA_ERROR};

    std::size_t offset = 0;
    while (offset != *received) {
      const auto written = co_await destination.write(
          std::span<const std::byte>(buffer).subspan(
              offset, *received - offset));
      if (!written.status.is_ok())
        co_return written.status;
      if (written.transferred == 0 ||
          written.transferred > *received - offset)
        co_return ntl::status{STATUS_DATA_ERROR};
      offset += written.transferred;
      transferred += written.transferred;
    }
  }
}

waitable_status_task run_proxy_session(
    std::shared_ptr<ntl::net::io::transport_backend> client_backend,
    std::shared_ptr<ntl::net::io::transport_backend> server_backend,
    const std::atomic<bool> &stopping, std::uint64_t &bytes_to_origin,
    std::uint64_t &bytes_to_client) {
  co_return co_await ntl::net::io::with_async_transport(
      std::move(client_backend), 64 * 1024,
      [&](std::shared_ptr<ntl::net::io::async_transport_stream> client)
          -> ntl::net::kernel::task<ntl::status> {
        co_return co_await ntl::net::io::with_async_transport(
            std::move(server_backend), 64 * 1024,
            [&](std::shared_ptr<ntl::net::io::async_transport_stream> server)
                -> ntl::net::kernel::task<ntl::status> {
              co_return co_await ntl::net::kernel::join_bidirectional(
                  relay(*client, *server, stopping, bytes_to_origin),
                  relay(*server, *client, stopping, bytes_to_client),
                  [client, server]() noexcept {
                    client->close();
                    server->close();
                  },
                  [](ntl::status) noexcept { return false; });
            });
      });
}

class proxy_state {
public:
  ntl::status start() noexcept {
    process_id_ = HandleToULong(PsGetCurrentProcessId());
    if (process_id_ == 0)
      return STATUS_INVALID_CID;
    const ntl::status opened = provider_.open({5'000});
    if (!opened.is_ok())
      return opened;
    auto v4 = ntl::net::kernel::wsk_tcp_listener::try_listen(
        provider_, ntl::net::kernel::ip_endpoint::any_ipv4(),
        listener_limits());
    if (!v4) {
      provider_.close();
      return v4.status();
    }
    listener_v4_ = std::move(*v4);
    auto v6 = ntl::net::kernel::wsk_tcp_listener::try_listen(
        provider_, ntl::net::kernel::ip_endpoint::any_ipv6(),
        listener_limits());
    if (!v6) {
      listener_v4_.reset();
      provider_.close();
      return v6.status();
    }
    listener_v6_ = std::move(*v6);
    worker_v4_.owner = this;
    worker_v4_.listener = listener_v4_.get();
    worker_v6_.owner = this;
    worker_v6_.listener = listener_v6_.get();

    auto thread_v4 = ntl::system_thread::create(&worker_entry, &worker_v4_);
    if (!thread_v4) {
      shutdown();
      return thread_v4.status();
    }
    thread_v4_ = std::move(*thread_v4);
    auto thread_v6 = ntl::system_thread::create(&worker_entry, &worker_v6_);
    if (!thread_v6) {
      shutdown();
      return thread_v6.status();
    }
    thread_v6_ = std::move(*thread_v6);
    started_.store(true, std::memory_order_release);
    return ntl::status::ok();
  }

  contract::proxy_info snapshot() const noexcept {
    return {
        process_id_,
        listener_v4_ ? listener_v4_->local_endpoint().port()
                     : std::uint16_t{0},
        listener_v6_ ? listener_v6_->local_endpoint().port()
                     : std::uint16_t{0},
        accepted_.load(std::memory_order_relaxed),
        redirect_records_.load(std::memory_order_relaxed),
        completed_.load(std::memory_order_relaxed),
        failed_.load(std::memory_order_relaxed),
        bytes_to_origin_.load(std::memory_order_relaxed),
        bytes_to_client_.load(std::memory_order_relaxed),
    };
  }

  void shutdown() noexcept {
    if (stopping_.exchange(true, std::memory_order_acq_rel))
      return;
    if (listener_v4_)
      listener_v4_->stop();
    if (listener_v6_)
      listener_v6_->stop();
    if (thread_v4_)
      (void)thread_v4_.join();
    if (thread_v6_)
      (void)thread_v6_.join();
    listener_v4_.reset();
    listener_v6_.reset();
    provider_.close();
    started_.store(false, std::memory_order_release);
  }

private:
  struct worker_context {
    proxy_state *owner = nullptr;
    ntl::net::kernel::wsk_tcp_listener *listener = nullptr;
  };

  static ntl::net::kernel::wsk_listener_limits listener_limits() noexcept {
    return {
        .connection =
            {.maximum_write_bytes = 64 * 1024,
             .receive_buffer_bytes = 64 * 1024,
             .connect_timeout = std::chrono::seconds(5)},
        .accept_timeout = (std::chrono::milliseconds::max)(),
    };
  }

  static void worker_entry(void *context) noexcept {
    auto *worker = static_cast<worker_context *>(context);
    if (worker && worker->owner && worker->listener)
      worker->owner->run(*worker->listener);
    PsTerminateSystemThread(STATUS_SUCCESS);
  }

  void run(ntl::net::kernel::wsk_tcp_listener &listener) noexcept {
    while (!stopping_.load(std::memory_order_acquire)) {
      auto accepted = listener.accept();
      if (!accepted) {
        const NTSTATUS status = static_cast<NTSTATUS>(accepted.status());
        if (status == STATUS_IO_TIMEOUT)
          continue;
        if (!stopping_.load(std::memory_order_acquire))
          failed_.fetch_add(1, std::memory_order_relaxed);
        break;
      }
      accepted_.fetch_add(1, std::memory_order_relaxed);
      auto handoff =
          ntl::net::kernel::wsk_redirected_connection::capture(**accepted);
      if (!handoff) {
        failed_.fetch_add(1, std::memory_order_relaxed);
        continue;
      }
      redirect_records_.fetch_add(1, std::memory_order_relaxed);
      auto outbound = handoff->try_connect_original(
          provider_, listener_limits().connection);
      if (!outbound) {
        failed_.fetch_add(1, std::memory_order_relaxed);
        continue;
      }

      std::uint64_t bytes_to_origin = 0;
      std::uint64_t bytes_to_client = 0;
      auto operation = run_proxy_session(
          std::move(*accepted), std::move(*outbound), stopping_,
          bytes_to_origin, bytes_to_client);
      const ntl::status result = operation.wait();
      bytes_to_origin_.fetch_add(bytes_to_origin, std::memory_order_relaxed);
      bytes_to_client_.fetch_add(bytes_to_client, std::memory_order_relaxed);
      if (result.is_ok())
        completed_.fetch_add(1, std::memory_order_relaxed);
      else if (!stopping_.load(std::memory_order_acquire))
        failed_.fetch_add(1, std::memory_order_relaxed);
    }
  }

  ntl::net::kernel::wsk_provider provider_{};
  ntl::net::kernel::wsk_tcp_listener::pointer listener_v4_{};
  ntl::net::kernel::wsk_tcp_listener::pointer listener_v6_{};
  worker_context worker_v4_{};
  worker_context worker_v6_{};
  ntl::system_thread thread_v4_{};
  ntl::system_thread thread_v6_{};
  std::uint32_t process_id_ = 0;
  std::atomic<std::uint64_t> accepted_{0};
  std::atomic<std::uint64_t> redirect_records_{0};
  std::atomic<std::uint64_t> completed_{0};
  std::atomic<std::uint64_t> failed_{0};
  std::atomic<std::uint64_t> bytes_to_origin_{0};
  std::atomic<std::uint64_t> bytes_to_client_{0};
  std::atomic<bool> stopping_{false};
  std::atomic<bool> started_{false};
};

template <class Layer>
ntl::wfp::terminating_decision redirect_connection(
    ntl::wfp::connect_redirector &redirector,
    const ntl::wfp::classify_event<Layer> &event) noexcept {
  const auto protocol = event.value(Layer::field::protocol).uint8();
  if (!protocol || *protocol != IPPROTO_TCP)
    return ntl::wfp::terminating_decision::block;
  return redirector.redirect(
      event, ntl::wfp::local_proxy_target::from_filter_context(
                 event.filter().context()));
}

} // namespace

ntl::status ntl::main(ntl::driver &driver, const std::wstring &) {
  auto state = std::make_shared<proxy_state>();
  const ntl::status started = state->start();
  if (!started.is_ok())
    return started;

  auto redirect = ntl::wfp::connect_redirector::try_create(
      contract::provider_key);
  if (!redirect) {
    state->shutdown();
    return redirect.status();
  }
  auto redirector = std::make_shared<ntl::wfp::connect_redirector>(
      std::move(*redirect));

  auto options = ntl::device_options()
                     .name(contract::device_name)
                     .type(FILE_DEVICE_UNKNOWN)
                     .exclusive(false)
                     .security_descriptor(L"D:P(A;;GA;;;SY)(A;;GA;;;BA)",
                                          contract::device_class_guid);
  auto endpoint_result =
      ntl::try_create_device_endpoint<void>(driver, options);
  if (!endpoint_result) {
    state->shutdown();
    return endpoint_result.status();
  }
  auto endpoint = std::move(*endpoint_result);
  const ntl::status query_route =
      endpoint.on_ioctl<contract::query_proxy_contract>(
          [state](contract::proxy_info &output) noexcept {
            output = state->snapshot();
            return ntl::status::ok();
          });
  if (!query_route.is_ok()) {
    state->shutdown();
    return query_route;
  }

  ntl::wfp::callout_driver<> callouts(driver);
  const ntl::status v4 = callouts.add_terminating(
      contract::callout_key_v4, redirector,
      [](ntl::wfp::connect_redirector &owned_redirector,
         const ntl::wfp::classify_event<contract::layer_v4> &event) noexcept {
        return redirect_connection(owned_redirector, event);
      });
  if (!v4.is_ok()) {
    state->shutdown();
    return v4;
  }
  const ntl::status v6 = callouts.add_terminating(
      contract::callout_key_v6, redirector,
      [](ntl::wfp::connect_redirector &owned_redirector,
         const ntl::wfp::classify_event<contract::layer_v6> &event) noexcept {
        return redirect_connection(owned_redirector, event);
      });
  if (!v6.is_ok()) {
    state->shutdown();
    return v6;
  }

  driver.on_unload([state, endpoint, callouts]() mutable {
    const ntl::status closed = endpoint.close();
    NT_ASSERT(closed.is_ok());
    const ntl::status reset = callouts.close();
    NT_ASSERT(reset.is_ok());
    state->shutdown();
  });
  return ntl::status::ok();
}
