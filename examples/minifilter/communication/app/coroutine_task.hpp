#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>

#include <ntl/handle>

#include <coroutine>
#include <exception>
#include <memory>
#include <optional>
#include <stdexcept>
#include <system_error>
#include <utility>

namespace crtsys_minifilter_communication_sample {

// The standard supplies coroutine machinery but no task return type. This
// example task starts immediately and exposes one blocking get() only so the
// console entry point can observe the resumed result.
template <typename T> class coroutine_task {
public:
  struct promise_type;
  struct shared_state {
    shared_state() : completed(::CreateEventW(nullptr, TRUE, FALSE, nullptr)) {
      if (!completed)
        throw std::system_error(
            static_cast<int>(::GetLastError()), std::system_category(),
            "Minifilter sample coroutine event creation failed");
    }
    ntl::unique_handle completed;
    std::optional<T> result;
    std::exception_ptr exception;
  };

  coroutine_task(const coroutine_task &) = delete;
  coroutine_task &operator=(const coroutine_task &) = delete;
  coroutine_task(coroutine_task &&other) noexcept
      : state_(std::move(other.state_)) {}

  T get() {
    if (!state_)
      throw std::logic_error("Minifilter coroutine has no result");
    auto state = std::exchange(state_, {});
    if (::WaitForSingleObject(state->completed.get(), INFINITE) !=
        WAIT_OBJECT_0)
      throw std::system_error(static_cast<int>(::GetLastError()),
                              std::system_category(),
                              "Minifilter sample coroutine wait failed");
    if (state->exception)
      std::rethrow_exception(state->exception);
    if (!state->result)
      throw std::logic_error("Minifilter coroutine produced no result");
    return std::move(*state->result);
  }

  struct promise_type {
    coroutine_task get_return_object() noexcept {
      return coroutine_task(state);
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

private:
  explicit coroutine_task(std::shared_ptr<shared_state> state) noexcept
      : state_(std::move(state)) {}
  std::shared_ptr<shared_state> state_;
};

} // namespace crtsys_minifilter_communication_sample
