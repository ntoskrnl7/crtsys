#pragma once

// Test-only eager bridge for driving several independent loopback peers.
// Public example code uses ntl::net::user::task and structured_concurrency.

#include <coroutine>
#include <exception>
#include <memory>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <utility>

#include "windows_support.hpp"

namespace crtsys::wfp_sample {

template <class T> class coroutine_task {
public:
  struct promise_type;

  struct shared_state {
    shared_state()
        : completed(::CreateEventW(nullptr, TRUE, FALSE, nullptr)) {
      if (!completed)
        throw_windows("CreateEventW");
    }
    ~shared_state() {
      if (completed)
        (void)::CloseHandle(completed);
    }

    template <class U> void publish_value(U &&value) {
      std::coroutine_handle<> continuation;
      {
        std::lock_guard lock(completion_lock);
        result.emplace(std::forward<U>(value));
        ready = true;
        continuation =
            std::exchange(awaiting_coroutine, {});
      }
      (void)::SetEvent(completed);
      if (continuation)
        continuation.resume();
    }

    void publish_exception(std::exception_ptr failure) noexcept {
      std::coroutine_handle<> continuation;
      {
        std::lock_guard lock(completion_lock);
        exception = std::move(failure);
        ready = true;
        continuation =
            std::exchange(awaiting_coroutine, {});
      }
      (void)::SetEvent(completed);
      if (continuation)
        continuation.resume();
    }

    bool is_ready() const {
      std::lock_guard lock(completion_lock);
      return ready;
    }

    bool suspend(std::coroutine_handle<> continuation) {
      std::lock_guard lock(completion_lock);
      if (ready)
        return false;
      if (awaiting_coroutine)
        throw std::logic_error(
            "coroutine task already has an awaiting coroutine");
      awaiting_coroutine = continuation;
      return true;
    }

    T take_result() {
      std::lock_guard lock(completion_lock);
      if (exception)
        std::rethrow_exception(exception);
      if (!result)
        throw std::logic_error("coroutine produced no result");
      return std::move(*result);
    }

    HANDLE completed = nullptr;
    mutable std::mutex completion_lock;
    bool ready = false;
    std::coroutine_handle<> awaiting_coroutine{};
    std::optional<T> result;
    std::exception_ptr exception;
  };

  coroutine_task(const coroutine_task &) = delete;
  coroutine_task &operator=(const coroutine_task &) = delete;
  coroutine_task(coroutine_task &&) noexcept = default;
  coroutine_task &operator=(coroutine_task &&) noexcept = default;

  class awaiter {
  public:
    explicit awaiter(
        std::shared_ptr<shared_state> state) noexcept
        : state_(std::move(state)) {}

    bool await_ready() const { return state_->is_ready(); }

    bool await_suspend(std::coroutine_handle<> continuation) {
      return state_->suspend(continuation);
    }

    T await_resume() {
      return state_->take_result();
    }

  private:
    std::shared_ptr<shared_state> state_;
  };

  awaiter operator co_await() && {
    if (!state_)
      throw std::logic_error("coroutine task has no state");
    return awaiter(std::exchange(state_, {}));
  }

  T get() {
    if (!state_)
      throw std::logic_error("coroutine task has no state");
    auto state = std::exchange(state_, {});
    if (::WaitForSingleObject(state->completed, INFINITE) !=
        WAIT_OBJECT_0)
      throw_windows("WaitForSingleObject");
    return state->take_result();
  }

  struct promise_type {
    coroutine_task get_return_object() noexcept {
      return coroutine_task(state);
    }
    std::suspend_never initial_suspend() const noexcept { return {}; }
    std::suspend_never final_suspend() const noexcept { return {}; }
    template <class U> void return_value(U &&value) {
      state->publish_value(std::forward<U>(value));
    }
    void unhandled_exception() noexcept {
      state->publish_exception(std::current_exception());
    }
    std::shared_ptr<shared_state> state =
        std::make_shared<shared_state>();
  };

private:
  explicit coroutine_task(
      std::shared_ptr<shared_state> state) noexcept
      : state_(std::move(state)) {}
  std::shared_ptr<shared_state> state_;
};

template <class T> class nested_task {
public:
  struct promise_type;
  using handle_type = std::coroutine_handle<promise_type>;

  nested_task(const nested_task &) = delete;
  nested_task &operator=(const nested_task &) = delete;
  nested_task(nested_task &&other) noexcept
      : handle_(std::exchange(other.handle_, {})) {}
  ~nested_task() {
    if (handle_)
      handle_.destroy();
  }

  class awaiter {
  public:
    explicit awaiter(handle_type handle) noexcept : handle_(handle) {}
    awaiter(const awaiter &) = delete;
    awaiter &operator=(const awaiter &) = delete;
    awaiter(awaiter &&other) noexcept
        : handle_(std::exchange(other.handle_, {})) {}
    ~awaiter() {
      if (handle_)
        handle_.destroy();
    }
    bool await_ready() const noexcept { return false; }
    std::coroutine_handle<>
    await_suspend(std::coroutine_handle<> next) noexcept {
      handle_.promise().continuation = next;
      return handle_;
    }
    T await_resume() {
      auto &promise = handle_.promise();
      if (promise.exception)
        std::rethrow_exception(promise.exception);
      if (!promise.value)
        throw std::logic_error("nested coroutine produced no result");
      return std::move(*promise.value);
    }

  private:
    handle_type handle_{};
  };

  awaiter operator co_await() && noexcept {
    return awaiter(std::exchange(handle_, {}));
  }

  struct promise_type {
    nested_task get_return_object() noexcept {
      return nested_task(handle_type::from_promise(*this));
    }
    std::suspend_always initial_suspend() const noexcept { return {}; }
    struct final_awaiter {
      bool await_ready() const noexcept { return false; }
      std::coroutine_handle<>
      await_suspend(handle_type handle) const noexcept {
        const auto next = handle.promise().continuation;
        return next ? next : std::noop_coroutine();
      }
      void await_resume() const noexcept {}
    };
    final_awaiter final_suspend() const noexcept { return {}; }
    template <class U> void return_value(U &&value) {
      this->value.emplace(std::forward<U>(value));
    }
    void unhandled_exception() noexcept {
      exception = std::current_exception();
    }

    std::coroutine_handle<> continuation{};
    std::optional<T> value;
    std::exception_ptr exception;
  };

private:
  explicit nested_task(handle_type handle) noexcept : handle_(handle) {}
  handle_type handle_{};
};

} // namespace crtsys::wfp_sample
