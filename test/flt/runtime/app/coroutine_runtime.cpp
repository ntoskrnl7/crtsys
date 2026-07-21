#include "coroutine_runtime.hpp"

#include "../shared/runtime_test.hpp"

#if (defined(_MSVC_LANG) && _MSVC_LANG >= 202002L) ||                       \
    (!defined(_MSVC_LANG) && __cplusplus >= 202002L)

#include <Windows.h>

#include <ntl/flt/communication_client>
#include <ntl/flt/coroutine>
#include <ntl/handle>

#include <coroutine>
#include <exception>
#include <memory>
#include <optional>
#include <stdexcept>
#include <system_error>
#include <utility>

namespace crtsys_flt_runtime_test {
namespace {

template <typename T> class runtime_coroutine_task {
public:
  struct shared_state {
    shared_state() : completed(::CreateEventW(nullptr, TRUE, FALSE, nullptr)) {
      if (!completed)
        throw std::system_error(
            static_cast<int>(::GetLastError()), std::system_category(),
            "Minifilter runtime coroutine event creation failed");
    }

    ntl::unique_handle completed;
    std::optional<T> result;
    std::exception_ptr exception;
  };

  struct promise_type {
    runtime_coroutine_task get_return_object() noexcept {
      return runtime_coroutine_task(state);
    }
    std::suspend_never initial_suspend() const noexcept { return {}; }
    std::suspend_never final_suspend() const noexcept { return {}; }

    template <typename U> void return_value(U &&value) {
      state->result.emplace(std::forward<U>(value));
      (void)::SetEvent(state->completed.get());
    }

    void unhandled_exception() noexcept {
      state->exception = std::current_exception();
      (void)::SetEvent(state->completed.get());
    }

    std::shared_ptr<shared_state> state = std::make_shared<shared_state>();
  };

  runtime_coroutine_task(const runtime_coroutine_task &) = delete;
  runtime_coroutine_task &operator=(const runtime_coroutine_task &) = delete;
  runtime_coroutine_task(runtime_coroutine_task &&) noexcept = default;

  T get() {
    if (!state_ || ::WaitForSingleObject(state_->completed.get(), INFINITE) !=
                       WAIT_OBJECT_0)
      throw std::runtime_error("Minifilter runtime coroutine did not complete");
    auto state = std::move(state_);
    if (state->exception)
      std::rethrow_exception(state->exception);
    if (!state->result)
      throw std::runtime_error("Minifilter runtime coroutine returned no value");
    return std::move(*state->result);
  }

private:
  explicit runtime_coroutine_task(std::shared_ptr<shared_state> state) noexcept
      : state_(std::move(state)) {}

  std::shared_ptr<shared_state> state_;
};

runtime_coroutine_task<std::uint32_t>
coroutine_ping(ntl::flt::communication_client &client) {
  co_return co_await ping_async(client, std::uint32_t{28});
}

runtime_coroutine_task<progress_event>
coroutine_notification(ntl::flt::communication_client &client) {
  co_return co_await progress_async(client);
}

using number_delivery =
    ntl::rpc::notification_delivery<numbers_stream_type::record_type>;

runtime_coroutine_task<number_delivery>
coroutine_stream(ntl::flt::communication_client_stream<numbers_stream_type> stream) {
  co_return co_await stream.read_async();
}

} // namespace

bool run_coroutine_runtime_tests(ntl::flt::communication_client &client) {
  if (coroutine_ping(client).get() != 29)
    return false;

  auto notification = coroutine_notification(client);
  if (client.invoke(publish_progress_method, std::uint32_t{61}, false) != 0)
    return false;
  if (notification.get().value != 61)
    return false;

  auto stream = numbers(client);
  stream.write(std::uint32_t{20});
  auto result = coroutine_stream(std::move(stream)).get();
  if (!result.payload().has_value() || result.payload().value() != 120)
    return false;
  // The stream object is moved into the coroutine. Its owner closes when the
  // coroutine result is destroyed after the receive has completed.
  return true;
}

} // namespace crtsys_flt_runtime_test

#else

#include <ntl/flt/communication_client>

namespace crtsys_flt_runtime_test {

bool run_coroutine_runtime_tests(ntl::flt::communication_client &) {
  // C++14/17 consumers retain the synchronous and explicit async APIs. The
  // coroutine adapter is intentionally compiled only for C++20 consumers.
  return true;
}

} // namespace crtsys_flt_runtime_test

#endif
