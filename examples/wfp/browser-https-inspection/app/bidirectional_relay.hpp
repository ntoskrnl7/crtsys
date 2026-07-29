#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>

#include <coroutine>
#include <cstddef>
#include <exception>
#include <memory>
#include <mutex>
#include <utility>

#include "coroutine_task.hpp"

namespace crtsys::wfp_sample::browser_https {

/**
 * Joins two already-running relay coroutines without blocking their IOCP
 * completion thread or creating helper threads.
 *
 * Both relay tasks are already running. The first one to finish shuts down
 * both sockets. The parent resumes only after the peer relay has observed that
 * shutdown and completed too.
 */
class bidirectional_relay_join {
public:
  bidirectional_relay_join(
      coroutine_task<std::size_t> first,
      coroutine_task<std::size_t> second,
      SOCKET first_socket, SOCKET second_socket)
      : state_(std::make_shared<shared_state>(
            first_socket, second_socket)),
        first_observer_(observe(state_, std::move(first))),
        second_observer_(observe(state_, std::move(second))) {}

  bidirectional_relay_join(
      const bidirectional_relay_join &) = delete;
  bidirectional_relay_join &
  operator=(const bidirectional_relay_join &) = delete;
  bidirectional_relay_join(
      bidirectional_relay_join &&) = delete;
  bidirectional_relay_join &
  operator=(bidirectional_relay_join &&) = delete;

  bool await_ready() const { return state_->all_completed(); }

  bool await_suspend(std::coroutine_handle<> continuation) {
    return state_->suspend(continuation);
  }

  void await_resume() {
    const auto failure = state_->failure();
    if (failure)
      std::rethrow_exception(failure);
  }

private:
  struct shared_state {
    shared_state(SOCKET first, SOCKET second) noexcept
        : first_socket(first), second_socket(second) {}

    void complete(std::exception_ptr failure) noexcept {
      bool leader = false;
      {
        std::lock_guard lock(state_lock);
        leader = !stopping;
        if (leader) {
          stopping = true;
          primary_failure = std::move(failure);
        }
      }

      if (leader) {
        (void)::shutdown(first_socket, SD_BOTH);
        (void)::shutdown(second_socket, SD_BOTH);
      }

      std::coroutine_handle<> continuation_to_resume;
      {
        std::lock_guard lock(state_lock);
        ++completed;
        if (completed == 2)
          continuation_to_resume =
              std::exchange(continuation, {});
      }
      if (continuation_to_resume)
        continuation_to_resume.resume();
    }

    bool all_completed() const {
      std::lock_guard lock(state_lock);
      return completed == 2;
    }

    bool suspend(std::coroutine_handle<> next) {
      std::lock_guard lock(state_lock);
      if (completed == 2)
        return false;
      continuation = next;
      return true;
    }

    std::exception_ptr failure() const {
      std::lock_guard lock(state_lock);
      return primary_failure;
    }

    SOCKET first_socket = INVALID_SOCKET;
    SOCKET second_socket = INVALID_SOCKET;
    mutable std::mutex state_lock;
    bool stopping = false;
    unsigned completed = 0;
    std::exception_ptr primary_failure;
    std::coroutine_handle<> continuation{};
  };

  static coroutine_task<std::size_t> observe(
      std::shared_ptr<shared_state> state,
      coroutine_task<std::size_t> task) {
    std::exception_ptr failure;
    std::size_t transferred = 0;
    try {
      transferred = co_await std::move(task);
    } catch (...) {
      failure = std::current_exception();
    }
    state->complete(std::move(failure));
    co_return transferred;
  }

  std::shared_ptr<shared_state> state_;
  coroutine_task<std::size_t> first_observer_;
  coroutine_task<std::size_t> second_observer_;
};

inline bidirectional_relay_join join_bidirectional_relays(
    coroutine_task<std::size_t> first,
    coroutine_task<std::size_t> second,
    SOCKET first_socket, SOCKET second_socket) {
  return bidirectional_relay_join(
      std::move(first), std::move(second),
      first_socket, second_socket);
}

} // namespace crtsys::wfp_sample::browser_https
