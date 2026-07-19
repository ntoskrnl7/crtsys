#include <gtest/gtest.h>
#include <windows.h>

#include <atomic>
#include <chrono>
#include <coroutine>
#include <exception>
#include <memory>
#include <optional>
#include <stop_token>
#include <stdexcept>
#include <system_error>
#include <thread>
#include <utility>

#include <ntl/handle>
#include <ntl/rpc/coroutine>

// Expand the shared contract as user-mode client method descriptors.
#include "common/rpc.hpp"

extern "C" std::size_t ntl_rpc_cxx14_async_operation_size() noexcept;

namespace {

template <typename T> class test_task {
public:
  struct promise_type;

  struct shared_state {
    shared_state()
        : completed(::CreateEventW(nullptr, TRUE, FALSE, nullptr)) {
      if (!completed)
        throw std::system_error(static_cast<int>(::GetLastError()),
                                std::system_category(),
                                "RPC test coroutine event creation failed");
    }

    ntl::unique_handle completed;
    std::optional<T> result;
    std::exception_ptr exception;
  };

  test_task(const test_task &) = delete;
  test_task &operator=(const test_task &) = delete;

  explicit test_task(std::shared_ptr<shared_state> state) noexcept
      : state_(std::move(state)) {}

  test_task(test_task &&other) noexcept
      : state_(std::move(other.state_)) {}

  T get() {
    if (!state_)
      throw std::logic_error("RPC test coroutine has no result");

    auto state = std::exchange(state_, {});
    const DWORD wait = ::WaitForSingleObject(state->completed.get(), INFINITE);
    if (wait != WAIT_OBJECT_0)
      throw std::system_error(static_cast<int>(::GetLastError()),
                              std::system_category(),
                              "RPC test coroutine wait failed");

    if (state->exception)
      std::rethrow_exception(state->exception);
    if (!state->result)
      throw std::logic_error("RPC test coroutine produced no result");
    return std::move(*state->result);
  }

  struct promise_type {
    test_task get_return_object() noexcept {
      return test_task(state);
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
  std::shared_ptr<shared_state> state_;
};

class abandoned_task {
public:
  struct promise_type;
  using handle_type = std::coroutine_handle<promise_type>;

  explicit abandoned_task(handle_type coroutine) noexcept
      : coroutine_(coroutine) {}
  abandoned_task(const abandoned_task &) = delete;
  abandoned_task &operator=(const abandoned_task &) = delete;

  ~abandoned_task() {
    if (coroutine_)
      coroutine_.destroy();
  }

  struct promise_type {
    abandoned_task get_return_object() noexcept {
      return abandoned_task(handle_type::from_promise(*this));
    }
    std::suspend_never initial_suspend() const noexcept { return {}; }
    std::suspend_always final_suspend() const noexcept { return {}; }
    void return_void() const noexcept {}
    void unhandled_exception() const noexcept { std::terminate(); }
  };

private:
  handle_type coroutine_{};
};

test_task<std::uint32_t>
invoke_value(std::stop_token token, std::uint32_t delay, std::uint32_t value,
             std::atomic<unsigned> *resume_count = nullptr) {
  const auto result =
      co_await test_rpc::test_cooperative_delay_async(token, delay, value);
  if (resume_count)
    resume_count->fetch_add(1);
  co_return result;
}

test_task<int> invoke_void() {
  co_await test_rpc::test_void_async();
  co_return 42;
}

abandoned_task abandon_pending_call() {
  (void)co_await test_rpc::test_cooperative_delay_async(
      std::uint32_t{2000}, std::uint32_t{99});
}

void expect_cancelled(test_task<std::uint32_t> task) {
  try {
    (void)task.get();
    FAIL() << "cancelled coroutine RPC returned a value";
  } catch (const std::system_error &error) {
    EXPECT_EQ(error.code().value(), ERROR_OPERATION_ABORTED);
  }
}

} // namespace

TEST(ntl_rpc_coroutine, async_operation_layout_matches_cxx14_translation_unit) {
  EXPECT_EQ(ntl_rpc_cxx14_async_operation_size(),
            sizeof(ntl::rpc::detail::async_operation));
}

TEST(ntl_rpc_coroutine, awaits_typed_and_void_results) {
  std::stop_source source;
  std::atomic<unsigned> resumed{0};
  auto value = invoke_value(source.get_token(), 25, 42, &resumed);
  EXPECT_EQ(value.get(), 42u);
  EXPECT_EQ(resumed.load(), 1u);

  auto no_value = invoke_void();
  EXPECT_EQ(no_value.get(), 42);
}

TEST(ntl_rpc_coroutine, stopped_token_does_not_start_execution) {
  std::stop_source stopped;
  ASSERT_TRUE(stopped.request_stop());
  expect_cancelled(invoke_value(stopped.get_token(), 2000, 43));
}

TEST(ntl_rpc_coroutine, stop_token_cancels_during_execution) {
  using namespace std::chrono_literals;

  std::stop_source running;
  auto pending = invoke_value(running.get_token(), 2000, 44);
  std::this_thread::sleep_for(50ms);
  ASSERT_TRUE(running.request_stop());
  expect_cancelled(std::move(pending));
}

TEST(ntl_rpc_coroutine, abandoning_frame_cancels_only_its_request) {
  using namespace std::chrono_literals;

  ntl::rpc::client client(L"test_rpc");
  ASSERT_TRUE(client);

  {
    auto abandoned = abandon_pending_call();
    (void)abandoned;
  }

  std::this_thread::sleep_for(50ms);
  EXPECT_EQ(client.invoke(test_rpc::test_cooperative_delay_2_method,
                          std::uint32_t{0}, std::uint32_t{45}),
            45u);
}
