#include <ntl/net/io/async_socket>
#include <ntl/net/io/async_framed_stream>
#include <ntl/net/framing>

#include <array>
#include <chrono>
#include <condition_variable>
#include <coroutine>
#include <cstddef>
#include <exception>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <stdexcept>
#include <system_error>
#include <thread>
#include <type_traits>
#include <utility>

namespace {

struct compile_task {
  struct promise_type {
    compile_task get_return_object() const noexcept { return {}; }
    std::suspend_never initial_suspend() const noexcept { return {}; }
    std::suspend_never final_suspend() const noexcept { return {}; }
    void return_void() const noexcept {}
    void unhandled_exception() const noexcept {}
  };
};

[[maybe_unused]] compile_task
compile_socket_operations(ntl::net::async_socket &socket) {
  sockaddr_in destination{};
  destination.sin_family = AF_INET;
  co_await socket.connect(
      reinterpret_cast<const sockaddr *>(&destination),
      static_cast<int>(sizeof(destination)));

  std::array<std::byte, 8> input{};
  const std::size_t first = co_await socket.read_some_borrowed(input);
  (void)first;
  const std::size_t exact = co_await socket.read_exactly_borrowed(input);
  (void)exact;
  const std::size_t written =
      co_await socket.write_all(std::span<const std::byte>(input));
  (void)written;

  ntl::net::async_framed_stream framed(
      std::move(socket), ntl::net::framing::u32_be_length_prefix{1024});
  auto message = co_await framed.read_frame();
  (void)message;
}

static_assert(std::is_copy_constructible_v<
              ntl::net::io_completion_context>);
static_assert(std::is_move_constructible_v<
              ntl::net::io_completion_context>);
static_assert(!std::is_copy_constructible_v<
              ntl::net::async_socket>);
static_assert(std::is_move_constructible_v<
              ntl::net::async_socket>);

class winsock_session {
public:
  winsock_session() {
    WSADATA data{};
    const int status = ::WSAStartup(MAKEWORD(2, 2), &data);
    if (status != 0)
      throw std::system_error(status, std::system_category(), "WSAStartup");
  }
  ~winsock_session() { (void)::WSACleanup(); }
};

class socket_owner {
public:
  explicit socket_owner(SOCKET value = INVALID_SOCKET) noexcept
      : value_(value) {}
  socket_owner(const socket_owner &) = delete;
  socket_owner &operator=(const socket_owner &) = delete;
  socket_owner(socket_owner &&other) noexcept
      : value_(std::exchange(other.value_, INVALID_SOCKET)) {}
  ~socket_owner() {
    if (value_ != INVALID_SOCKET)
      (void)::closesocket(value_);
  }
  SOCKET get() const noexcept { return value_; }
  SOCKET release() noexcept {
    return std::exchange(value_, INVALID_SOCKET);
  }

private:
  SOCKET value_ = INVALID_SOCKET;
};

struct socket_pair {
  socket_owner client;
  socket_owner server;
};

socket_pair make_socket_pair() {
  socket_owner listener(::WSASocketW(AF_INET, SOCK_STREAM, IPPROTO_TCP,
                                     nullptr, 0, WSA_FLAG_OVERLAPPED));
  if (listener.get() == INVALID_SOCKET)
    throw std::system_error(::WSAGetLastError(), std::system_category(),
                            "WSASocketW(listener)");
  sockaddr_in address{};
  address.sin_family = AF_INET;
  address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  if (::bind(listener.get(), reinterpret_cast<const sockaddr *>(&address),
             sizeof(address)) == SOCKET_ERROR ||
      ::listen(listener.get(), 1) == SOCKET_ERROR)
    throw std::system_error(::WSAGetLastError(), std::system_category(),
                            "bind/listen");
  int address_size = sizeof(address);
  if (::getsockname(listener.get(), reinterpret_cast<sockaddr *>(&address),
                    &address_size) == SOCKET_ERROR)
    throw std::system_error(::WSAGetLastError(), std::system_category(),
                            "getsockname");

  socket_owner client(::WSASocketW(AF_INET, SOCK_STREAM, IPPROTO_TCP,
                                   nullptr, 0, WSA_FLAG_OVERLAPPED));
  if (client.get() == INVALID_SOCKET ||
      ::connect(client.get(), reinterpret_cast<const sockaddr *>(&address),
                sizeof(address)) == SOCKET_ERROR)
    throw std::system_error(::WSAGetLastError(), std::system_category(),
                            "connect");
  socket_owner server(::accept(listener.get(), nullptr, nullptr));
  if (server.get() == INVALID_SOCKET)
    throw std::system_error(::WSAGetLastError(), std::system_category(),
                            "accept");
  return {std::move(client), std::move(server)};
}

template <class T> class blocking_task {
public:
  struct promise_type;
  struct shared_state {
    std::mutex lock;
    std::condition_variable changed;
    std::optional<T> result;
    std::exception_ptr failure;
    bool complete = false;
  };

  blocking_task(const blocking_task &) = delete;
  blocking_task &operator=(const blocking_task &) = delete;
  blocking_task(blocking_task &&) noexcept = default;

  T get() {
    auto state = std::exchange(state_, {});
    if (!state)
      throw std::logic_error("blocking task has no state");
    std::unique_lock lock(state->lock);
    state->changed.wait(lock, [&] { return state->complete; });
    if (state->failure)
      std::rethrow_exception(state->failure);
    return std::move(*state->result);
  }

  struct promise_type {
    blocking_task get_return_object() noexcept { return blocking_task(state); }
    std::suspend_never initial_suspend() const noexcept { return {}; }
    std::suspend_never final_suspend() const noexcept { return {}; }
    template <class U> void return_value(U &&value) {
      {
        std::lock_guard lock(state->lock);
        state->result.emplace(std::forward<U>(value));
        state->complete = true;
      }
      state->changed.notify_all();
    }
    void unhandled_exception() noexcept {
      {
        std::lock_guard lock(state->lock);
        state->failure = std::current_exception();
        state->complete = true;
      }
      state->changed.notify_all();
    }
    std::shared_ptr<shared_state> state = std::make_shared<shared_state>();
  };

private:
  explicit blocking_task(std::shared_ptr<shared_state> state) noexcept
      : state_(std::move(state)) {}
  std::shared_ptr<shared_state> state_;
};

blocking_task<std::size_t> read_one(ntl::net::async_socket socket) {
  std::array<std::byte, 1> byte{};
  co_return co_await socket.read_some_borrowed(byte);
}

blocking_task<std::size_t> write_one(
    std::shared_ptr<ntl::net::async_socket> socket,
    std::array<std::byte, 1> value) {
  co_return co_await socket->write_all(value);
}

blocking_task<int> read_until_cancelled(ntl::net::async_socket socket) {
  try {
    std::array<std::byte, 1> byte{};
    (void)co_await socket.read_some_borrowed(byte);
    co_return 0;
  } catch (const std::system_error &error) {
    co_return error.code().value();
  }
}

using test_framed_stream =
    ntl::net::async_framed_stream<
        ntl::net::framing::u32_be_length_prefix>;

blocking_task<int>
read_frame_until_cancelled(test_framed_stream &stream) {
  try {
    (void)co_await stream.read_frame();
    co_return 0;
  } catch (const std::system_error &error) {
    co_return error.code().value();
  }
}

blocking_task<std::size_t> read_then_close_context(
    ntl::net::async_socket socket, ntl::net::io_completion_context context) {
  std::array<std::byte, 1> byte{};
  const std::size_t read = co_await socket.read_some_borrowed(byte);
  context.close();
  co_return read;
}

void require(bool condition, const char *message) {
  if (!condition)
    throw std::runtime_error(message);
}

void test_context_facade_can_die_first() {
  auto sockets = make_socket_pair();
  std::optional<ntl::net::async_socket> client;
  std::optional<ntl::net::async_socket> server;
  {
    ntl::net::io_completion_context context;
    client.emplace(context, sockets.client.release());
    server.emplace(context, sockets.server.release());
  }
  auto read = read_one(std::move(*server));
  server.reset();
  const std::array<std::byte, 1> value{std::byte{0x5a}};
  auto write_task = write_one(
      std::make_shared<ntl::net::async_socket>(client->share()), value);
  require(write_task.get() == 1 && read.get() == 1,
          "child sockets did not retain the IOCP runtime");
  client.reset();
}

void test_socket_facade_can_die_with_pending_read() {
  auto sockets = make_socket_pair();
  ntl::net::io_completion_context context;
  ntl::net::async_socket server(context, sockets.server.release());
  auto pending = read_until_cancelled(std::move(server));
  context.close();
  require(pending.get() != 0,
          "closing IOCP did not complete the pending socket operation");
}

void test_duplicate_close_is_idempotent() {
  auto sockets = make_socket_pair();
  ntl::net::io_completion_context context;
  ntl::net::async_socket server(context, sockets.server.release());
  auto pending = read_until_cancelled(std::move(server));
  auto first = context.share();
  auto second = context.share();
  std::thread a([value = std::move(first)]() mutable { value.close(); });
  std::thread b([value = std::move(second)]() mutable { value.close(); });
  a.join();
  b.join();
  context.close();
  require(pending.get() != 0, "concurrent close lost pending completion");
}

void test_close_from_completion_callback() {
  auto sockets = make_socket_pair();
  ntl::net::io_completion_context context;
  ntl::net::async_socket server(context, sockets.server.release());
  auto operation =
      read_then_close_context(std::move(server), context.share());
  const char value = 'x';
  if (::send(sockets.client.get(), &value, 1, 0) != 1)
    throw std::system_error(::WSAGetLastError(), std::system_category(),
                            "send(callback-close)");
  require(operation.get() == 1,
          "completion-thread close deadlocked or lost the read");
  context.close();
}

void test_same_facade_close_use_race() {
  auto sockets = make_socket_pair();
  ntl::net::io_completion_context context;
  ntl::net::async_socket server(context, sockets.server.release());
  std::atomic<bool> start{false};
  std::atomic<unsigned> retained{0};
  std::atomic<bool> rejected{false};
  std::thread user([&] {
    while (!start.load(std::memory_order_acquire))
      std::this_thread::yield();
    for (;;) {
      auto child = server.share();
      if (!child) {
        rejected.store(true, std::memory_order_release);
        return;
      }
      retained.fetch_add(1, std::memory_order_release);
      (void)child.cancel();
    }
  });
  start.store(true, std::memory_order_release);
  while (retained.load(std::memory_order_acquire) == 0)
    std::this_thread::yield();
  server.close();
  server.close();
  user.join();
  require(rejected.load(std::memory_order_acquire),
          "same-facade socket close/use did not reject new ownership");
  context.close();
}

void test_framed_stream_same_facade_close_read_race() {
  auto sockets = make_socket_pair();
  ntl::net::io_completion_context context;
  ntl::net::async_socket server(context, sockets.server.release());
  test_framed_stream stream(
      std::move(server),
      ntl::net::framing::u32_be_length_prefix{1024},
      {.maximum_frame_size = 1024}, 64);

  auto pending = read_frame_until_cancelled(stream);
  std::atomic<bool> stop_observer{false};
  std::thread observer([&] {
    while (!stop_observer.load(std::memory_order_acquire)) {
      (void)stream.buffered_size();
      (void)stream.maximum_frame_size();
    }
  });
  std::thread first_close([&] { stream.close(); });
  std::thread second_close([&] { stream.close(); });
  first_close.join();
  second_close.join();
  stop_observer.store(true, std::memory_order_release);
  observer.join();

  require(pending.get() != 0,
          "framed stream close did not cancel its pending read");
  bool rejected = false;
  try {
    (void)ntl::net::user::sync_wait(stream.read_frame());
  } catch (const std::system_error &) {
    rejected = true;
  }
  require(rejected,
          "closed framed stream accepted a new frame read");
  context.close();
}

} // namespace

int main() {
  try {
    winsock_session winsock;
    test_context_facade_can_die_first();
    test_socket_facade_can_die_with_pending_read();
    test_duplicate_close_is_idempotent();
    test_close_from_completion_callback();
    test_same_facade_close_use_race();
    test_framed_stream_same_facade_close_read_race();
    return 0;
  } catch (...) {
    return 1;
  }
}
