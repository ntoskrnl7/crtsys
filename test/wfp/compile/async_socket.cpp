#include <ntl/net/io/async_socket>
#include <ntl/net/io/async_framed_stream>
#include <ntl/net/framing>

#include <array>
#include <coroutine>
#include <cstddef>
#include <span>
#include <type_traits>

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
  std::array<std::byte, 8> input{};
  const std::size_t first = co_await socket.read_some(input);
  (void)first;
  const std::size_t exact = co_await socket.read_exactly(input);
  (void)exact;
  const std::size_t written =
      co_await socket.write_all(std::span<const std::byte>(input));
  (void)written;

  ntl::net::async_framed_stream framed(
      socket, ntl::net::framing::u32_be_length_prefix{1024});
  auto message = co_await framed.read_frame();
  (void)message;
}

static_assert(!std::is_copy_constructible_v<
              ntl::net::io_completion_context>);
static_assert(!std::is_move_constructible_v<
              ntl::net::io_completion_context>);
static_assert(!std::is_copy_constructible_v<
              ntl::net::async_socket>);
static_assert(std::is_move_constructible_v<
              ntl::net::async_socket>);

} // namespace

int main() { return 0; }
