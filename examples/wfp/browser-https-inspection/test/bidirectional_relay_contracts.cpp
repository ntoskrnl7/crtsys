#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>

#include <array>
#include <atomic>
#include <coroutine>
#include <cstddef>
#include <exception>
#include <iostream>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

#include <ntl/net/io/async_socket>

#include "bidirectional_relay.hpp"

namespace sample = crtsys::wfp_sample;
namespace browser = crtsys::wfp_sample::browser_https;

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

sample::coroutine_task<std::size_t> immediate_relay(
    std::size_t transferred) {
  co_return transferred;
}

sample::coroutine_task<std::size_t> gated_relay(
    manual_gate &gate, std::size_t transferred,
    std::string_view failure = {}) {
  co_await gate;
  if (!failure.empty())
    throw std::runtime_error(std::string(failure));
  co_return transferred;
}

sample::coroutine_task<unsigned> join_immediate_relays() {
  co_await browser::join_bidirectional_relays(
      immediate_relay(11), immediate_relay(13),
      INVALID_SOCKET, INVALID_SOCKET);
  co_return 1;
}

sample::coroutine_task<unsigned> join_gated_relays(
    manual_gate &first, manual_gate &second,
    std::atomic<unsigned> &resume_count,
    std::string_view first_failure = {},
    std::string_view second_failure = {}) {
  co_await browser::join_bidirectional_relays(
      gated_relay(first, 17, first_failure),
      gated_relay(second, 19, second_failure),
      INVALID_SOCKET, INVALID_SOCKET);
  resume_count.fetch_add(1, std::memory_order_relaxed);
  co_return 1;
}

sample::coroutine_task<std::size_t> read_until_stopped(
    ntl::net::async_socket &socket) {
  std::array<std::byte, 1> buffer{};
  co_return co_await socket.read_some(buffer);
}

sample::coroutine_task<unsigned> join_socket_relays(
    ntl::net::async_socket &first,
    ntl::net::async_socket &second) {
  co_await browser::join_bidirectional_relays(
      immediate_relay(0), read_until_stopped(second),
      first.native_handle(), second.native_handle());
  co_return 1;
}

sample::coroutine_task<unsigned> join_gated_socket_relays(
    manual_gate &gate,
    ntl::net::async_socket &first,
    ntl::net::async_socket &second) {
  co_await browser::join_bidirectional_relays(
      gated_relay(gate, 0), read_until_stopped(second),
      first.native_handle(), second.native_handle());
  co_return 1;
}

void require(bool condition, std::string_view message) {
  if (!condition)
    throw std::runtime_error(std::string(message));
}

void test_already_completed_relays() {
  auto joined = join_immediate_relays();
  require(joined.get() == 1,
          "already-completed relays did not join");
}

void test_first_completion_waits_for_peer() {
  manual_gate first;
  manual_gate second;
  std::atomic<unsigned> resume_count{0};
  auto joined = join_gated_relays(first, second, resume_count);

  first.complete();
  require(resume_count.load(std::memory_order_relaxed) == 0,
          "join resumed before the peer relay completed");
  second.complete();
  require(resume_count.load(std::memory_order_relaxed) == 1,
          "join did not resume exactly once");
  require(joined.get() == 1, "joined relay result was lost");
}

void test_first_failure_wins() {
  manual_gate first;
  manual_gate second;
  std::atomic<unsigned> resume_count{0};
  auto joined = join_gated_relays(
      first, second, resume_count, "primary relay failure",
      "shutdown relay failure");

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
  auto joined = join_gated_relays(
      first, second, resume_count, {},
      "expected failure after peer shutdown");

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
  auto joined = join_gated_relays(first, second, resume_count);

  std::thread first_completion([&first] { first.complete(); });
  std::thread second_completion([&second] { second.complete(); });
  first_completion.join();
  second_completion.join();

  require(resume_count.load(std::memory_order_relaxed) == 1,
          "concurrent completions resumed the join more than once");
  require(joined.get() == 1,
          "concurrently completed relays did not join");
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
  auto joined = join_socket_relays(first, second);
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
        joined(join_gated_socket_relays(gate, first, second)) {}

  manual_gate gate;
  ntl::net::async_socket first;
  ntl::net::async_socket second;
  sample::coroutine_task<unsigned> joined;
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

  require(
      context.outstanding_operations() == connection_count,
      "shared IOCP did not retain every pending connection");
  for (const auto &relay : relays)
    relay->gate.complete();
  for (const auto &relay : relays)
    require(
        relay->joined.get() == 1,
        "shared-IOCP relay did not drain");
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
