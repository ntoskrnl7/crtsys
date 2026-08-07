#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <coroutine>
#include <exception>
#include <memory>
#include <optional>
#include <stdexcept>
#include <system_error>
#include <utility>

#include <ntl/handle>

namespace wfp_stream_edit_fixture {

template <class T> class coroutine_task {
public:
  struct promise_type;

  struct shared_state {
    shared_state() : completed(::CreateEventW(nullptr, TRUE, FALSE, nullptr)) {
      if (!completed)
        throw std::system_error(static_cast<int>(::GetLastError()),
                                std::system_category(),
                                "stream-edit fixture completion event");
    }
    ntl::unique_handle completed;
    std::optional<T> result;
    std::exception_ptr exception;
  };

  coroutine_task(const coroutine_task &) = delete;
  coroutine_task &operator=(const coroutine_task &) = delete;
  coroutine_task(coroutine_task &&other) noexcept
      : state_(std::move(other.state_)) {}
  coroutine_task &operator=(coroutine_task &&other) noexcept {
    state_ = std::move(other.state_);
    return *this;
  }

  T get() {
    if (!state_)
      throw std::logic_error("coroutine task has no state");
    auto state = std::exchange(state_, {});
    const DWORD wait = ::WaitForSingleObject(state->completed.get(), INFINITE);
    if (wait != WAIT_OBJECT_0)
      throw std::system_error(static_cast<int>(::GetLastError()),
                              std::system_category(),
                              "stream-edit fixture coroutine wait");
    if (state->exception)
      std::rethrow_exception(state->exception);
    if (!state->result)
      throw std::logic_error("coroutine task produced no result");
    return std::move(*state->result);
  }

  struct promise_type {
    coroutine_task get_return_object() noexcept { return coroutine_task(state); }
    std::suspend_never initial_suspend() const noexcept { return {}; }
    std::suspend_never final_suspend() const noexcept { return {}; }
    template <class U> void return_value(U &&value) {
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

} // namespace wfp_stream_edit_fixture
