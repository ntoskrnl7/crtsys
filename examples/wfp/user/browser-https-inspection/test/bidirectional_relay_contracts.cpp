#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>

#include <array>
#include <atomic>
#include <coroutine>
#include <cstddef>
#include <exception>
#include <future>
#include <iostream>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

#include <ntl/net/io/async_socket>
#include <ntl/net/user/structured_concurrency>

#include "windows_support.hpp"

namespace sample = crtsys::wfp_sample;

namespace {

class manual_gate {
public:
  class awaiter {
  public:
    explicit awaiter(manual_gate &gate) noexcept : gate_(&gate) {}

    bool await_ready() const {
      std::lock_guard lock(gate_->lock_);
      return gate_->ready_;
    }

    bool await_suspend(std::coroutine_handle<> continuation) {
      std::lock_guard lock(gate_->lock_);
      if (gate_->ready_)
        return false;
      gate_->continuation_ = continuation;
      return true;
    }

    void await_resume() const noexcept {}

  private:
    manual_gate *gate_;
  };

  awaiter operator co_await() noexcept { return awaiter(*this); }

  void complete() {
    std::coroutine_handle<> continuation;
    {
      std::lock_guard lock(lock_);
      if (ready_)
        throw std::logic_error("manual gate completed twice");
      ready_ = true;
      continuation = std::exchange(continuation_, {});
    }
    if (continuation)
      continuation.resume();
  }

private:
  friend class awaiter;
  std::mutex lock_;
  bool ready_ = false;
  std::coroutine_handle<> continuation_{};
};

ntl::net::user::task<std::size_t> immediate_relay(
    std::size_t transferred) {
  co_return transferred;
}

ntl::net::user::task<std::size_t> gated_relay(
    manual_gate &gate, std::size_t transferred,
    std::string_view failure = {}) {
  co_await gate;
  if (!failure.empty())
    throw std::runtime_error(std::string(failure));
  co_return transferred;
}

ntl::net::user::task<unsigned> observe_background_stop(
    manual_gate &gate, std::atomic<unsigned> &completed) {
  co_await gate;
  completed.fetch_add(1, std::memory_order_relaxed);
  co_return 7;
}

ntl::net::user::task<unsigned> join_immediate_relays() {
  co_await ntl::net::user::when_all_stop_on_first(
      immediate_relay(11), immediate_relay(13), []() noexcept {});
  co_return 1;
}

ntl::net::user::task<unsigned> join_gated_relays(
    manual_gate &first, manual_gate &second,
    std::atomic<unsigned> &resume_count,
    std::string_view first_failure = {},
    std::string_view second_failure = {}) {
  co_await ntl::net::user::when_all_stop_on_first(
      gated_relay(first, 17, first_failure),
      gated_relay(second, 19, second_failure), []() noexcept {});
  resume_count.fetch_add(1, std::memory_order_relaxed);
  co_return 1;
}

ntl::net::user::task<std::size_t> read_until_stopped(
    ntl::net::async_socket &socket) {
  std::array<std::byte, 1> buffer{};
  co_return co_await socket.read_some_borrowed(buffer);
}

ntl::net::user::task<unsigned> join_socket_relays(
    ntl::net::async_socket &first,
    ntl::net::async_socket &second) {
  co_await ntl::net::user::when_all_stop_on_first(
      immediate_relay(0), read_until_stopped(second),
      [&first, &second]() noexcept {
        const auto first_socket = first.borrowed_native_handle();
        const auto second_socket = second.borrowed_native_handle();
        if (first_socket != INVALID_SOCKET)
          (void)::shutdown(first_socket, SD_BOTH);
        if (second_socket != INVALID_SOCKET)
          (void)::shutdown(second_socket, SD_BOTH);
      });
  co_return 1;
}

ntl::net::user::task<unsigned> join_gated_socket_relays(
    manual_gate &gate,
    ntl::net::async_socket &first,
    ntl::net::async_socket &second) {
  co_await ntl::net::user::when_all_stop_on_first(
      gated_relay(gate, 0), read_until_stopped(second),
      [&first, &second]() noexcept {
        const auto first_socket = first.borrowed_native_handle();
        const auto second_socket = second.borrowed_native_handle();
        if (first_socket != INVALID_SOCKET)
          (void)::shutdown(first_socket, SD_BOTH);
        if (second_socket != INVALID_SOCKET)
          (void)::shutdown(second_socket, SD_BOTH);
      });
  co_return 1;
}

template <class T>
std::future<T> start_background(ntl::net::user::task<T> operation) {
  return std::async(
      std::launch::async,
      [operation = std::move(operation)]() mutable {
        return ntl::net::user::sync_wait(std::move(operation));
      });
}

void require(bool condition, std::string_view message) {
  if (!condition)
    throw std::runtime_error(std::string(message));
}

void test_already_completed_relays() {
  auto joined = start_background(join_immediate_relays());
  require(joined.get() == 1,
          "already-completed relays did not join");
}

void test_first_completion_waits_for_peer() {
  manual_gate first;
  manual_gate second;
  std::atomic<unsigned> resume_count{0};
  auto joined = start_background(
      join_gated_relays(first, second, resume_count));

  first.complete();
  require(resume_count.load(std::memory_order_relaxed) == 0,
          "join resumed before the peer relay completed");
  second.complete();
  require(joined.get() == 1, "joined relay result was lost");
  require(resume_count.load(std::memory_order_relaxed) == 1,
          "join did not resume exactly once");
}

void test_first_failure_wins() {
  manual_gate first;
  manual_gate second;
  std::atomic<unsigned> resume_count{0};
  auto joined = start_background(join_gated_relays(
      first, second, resume_count, "primary relay failure",
      "shutdown relay failure"));

  first.complete();
  second.complete();
  try {
    (void)joined.get();
  } catch (const std::runtime_error &error) {
    require(
        std::string_view(error.what()) == "primary relay failure",
        "join did not preserve the first failure");
    return;
  }
  throw std::runtime_error("join discarded the first relay failure");
}

void test_shutdown_failure_is_not_promoted() {
  manual_gate first;
  manual_gate second;
  std::atomic<unsigned> resume_count{0};
  auto joined = start_background(join_gated_relays(
      first, second, resume_count, {},
      "expected failure after peer shutdown"));

  first.complete();
  second.complete();
  require(joined.get() == 1,
          "peer shutdown noise replaced a normal first completion");
  require(resume_count.load(std::memory_order_relaxed) == 1,
          "normal join did not resume exactly once");
}

void test_concurrent_completion_resumes_once() {
  manual_gate first;
  manual_gate second;
  std::atomic<unsigned> resume_count{0};
  auto joined = start_background(
      join_gated_relays(first, second, resume_count));

  std::thread first_completion([&first] { first.complete(); });
  std::thread second_completion([&second] { second.complete(); });
  first_completion.join();
  second_completion.join();

  require(joined.get() == 1,
          "concurrently completed relays did not join");
  require(resume_count.load(std::memory_order_relaxed) == 1,
          "concurrent completions resumed the join more than once");
}

void test_background_close_is_owning_and_idempotent() {
  manual_gate gate;
  std::atomic<unsigned> stops{0};
  std::atomic<unsigned> completed{0};
  auto operation = ntl::net::user::start_background(
      observe_background_stop(gate, completed),
      [&]() noexcept {
        stops.fetch_add(1, std::memory_order_relaxed);
        gate.complete();
      });
  operation.close();
  operation.close();
  require(operation.get() == 7,
          "background operation lost its result during close");
  require(stops.load(std::memory_order_relaxed) == 1,
          "background stop callback was not idempotent");
  require(completed.load(std::memory_order_relaxed) == 1,
          "background child was not drained after close");
}

void test_first_completion_stops_socket_peer() {
  sample::winsock_session winsock;
  auto listener = sample::make_listener();
  sample::socket_owner client(::WSASocketW(
      AF_INET, SOCK_STREAM, IPPROTO_TCP, nullptr, 0,
      WSA_FLAG_OVERLAPPED));
  if (client.get() == INVALID_SOCKET)
    sample::throw_socket("WSASocketW(relay client)");

  sockaddr_in address{};
  address.sin_family = AF_INET;
  address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  address.sin_port = htons(listener.port);
  if (::connect(
          client.get(),
          reinterpret_cast<const sockaddr *>(&address),
          sizeof(address)) == SOCKET_ERROR)
    sample::throw_socket("connect(relay client)");
  auto server = sample::accept_one(listener);

  ntl::net::io_completion_context context;
  ntl::net::async_socket first(context, client.release());
  ntl::net::async_socket second(context, server.release());
  auto joined = start_background(join_socket_relays(first, second));
  require(joined.get() == 1,
          "socket-backed relay peer did not observe shutdown");
  context.wait_for_idle();
}

struct shared_context_relay {
  shared_context_relay(
      ntl::net::io_completion_context &context,
      sample::socket_owner first_native,
      sample::socket_owner second_native)
      : first(context, first_native.release()),
        second(context, second_native.release()),
        joined(start_background(
            join_gated_socket_relays(gate, first, second))) {}

  manual_gate gate;
  ntl::net::async_socket first;
  ntl::net::async_socket second;
  std::future<unsigned> joined;
};

void test_many_relays_share_one_completion_context() {
  constexpr std::size_t connection_count = 16;
  sample::winsock_session winsock;
  auto listener = sample::make_listener();
  ntl::net::io_completion_context context;
  std::vector<std::unique_ptr<shared_context_relay>> relays;
  relays.reserve(connection_count);

  for (std::size_t index = 0; index != connection_count; ++index) {
    sample::socket_owner client(::WSASocketW(
        AF_INET, SOCK_STREAM, IPPROTO_TCP, nullptr, 0,
        WSA_FLAG_OVERLAPPED));
    if (client.get() == INVALID_SOCKET)
      sample::throw_socket("WSASocketW(shared relay client)");

    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    address.sin_port = htons(listener.port);
    if (::connect(
            client.get(),
            reinterpret_cast<const sockaddr *>(&address),
            sizeof(address)) == SOCKET_ERROR)
      sample::throw_socket("connect(shared relay client)");
    auto server = sample::accept_one(listener);
    relays.push_back(std::make_unique<shared_context_relay>(
        context, std::move(client), std::move(server)));
  }

  for (unsigned attempt = 0;
       attempt != 2000 &&
       context.outstanding_operations() != connection_count;
       ++attempt)
    ::Sleep(1);
  const bool all_started =
      context.outstanding_operations() == connection_count;
  for (const auto &relay : relays)
    relay->gate.complete();
  for (const auto &relay : relays)
    require(
        relay->joined.get() == 1,
        "shared-IOCP relay did not drain");
  require(all_started,
          "shared IOCP did not retain every pending connection");
  context.wait_for_idle();
  require(
      context.outstanding_operations() == 0,
      "shared IOCP retained an operation after task drain");
}

} // namespace

int main() {
  try {
    test_already_completed_relays();
    test_first_completion_waits_for_peer();
    test_first_failure_wins();
    test_shutdown_failure_is_not_promoted();
    test_concurrent_completion_resumes_once();
    test_background_close_is_owning_and_idempotent();
    test_first_completion_stops_socket_peer();
    test_many_relays_share_one_completion_context();
    std::cout
        << "bidirectional relay coroutine contracts passed\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
