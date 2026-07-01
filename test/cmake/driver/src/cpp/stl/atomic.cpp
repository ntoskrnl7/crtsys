//
// https://en.cppreference.com/w/cpp/atomic/atomic#Example
//
#include <atomic>
#include <functional>
#include <iostream>
#include <mutex>
#include <numeric>
#include <stdexcept>
#include <thread>
#include <vector>

namespace atomic_test {
std::atomic_int acnt;
int cnt;

void f() {
  for (auto n{10000}; n; --n) {
    ++acnt;
    ++cnt;
    // Note: for this example, relaxed memory order is sufficient,
    // e.g. acnt.fetch_add(1, std::memory_order_relaxed);
  }
}

void run() {
  {
    acnt = 0;
    cnt = 0;
    std::vector<std::jthread> pool;
    for (int n = 0; n < 10; ++n) {
      pool.emplace_back(f);
    }
  }

  std::cout << "The atomic counter is " << acnt << '\n'
            << "The non-atomic counter is " << cnt << '\n';
}
} // namespace atomic_test

//
// https://en.cppreference.com/w/cpp/atomic/atomic_ref/atomic_ref#Example
//
namespace atomic_ref_test {
void run() {
#if defined(__cpp_lib_atomic_ref) && __cpp_lib_atomic_ref >= 201806L
  using Data = std::vector<char>;

  auto inc_atomically = [](Data &data) {
    for (Data::value_type &x : data) {
      auto xx = std::atomic_ref<Data::value_type>(x);
      ++xx; // atomic read-modify-write
    }
  };

  auto inc_directly = [](Data &data) {
    for (Data::value_type &x : data) {
      ++x;
    }
  };

  auto test_run = [](const auto Fun) {
    // cppreference uses Data data(10'000'000). The driver harness keeps the
    // same four-jthread atomic_ref pattern with a smaller resident buffer.
    Data data(100'000);
    {
      std::jthread j1{Fun, std::ref(data)};
      std::jthread j2{Fun, std::ref(data)};
      std::jthread j3{Fun, std::ref(data)};
      std::jthread j4{Fun, std::ref(data)};
    }
    std::cout << "sum = " << std::accumulate(cbegin(data), cend(data), 0)
              << '\n';
    return std::accumulate(cbegin(data), cend(data), 0);
  };

  const auto atomic_sum = test_run(inc_atomically);
  if (atomic_sum != 400'000) {
    throw std::runtime_error("unexpected atomic_ref sum");
  }
  test_run(inc_directly);
#else
  std::cout << "std::atomic_ref is not available in this STL\n";
#endif
}
} // namespace atomic_ref_test

//
// https://en.cppreference.com/w/cpp/atomic/atomic_flag#Example
//
namespace atomic_flag_test {
class mutex {
  std::atomic_flag m_{};

public:
  void lock() noexcept {
    while (m_.test_and_set(std::memory_order_acquire))
#if defined(__cpp_lib_atomic_wait) && __cpp_lib_atomic_wait >= 201907L
      // Since C++20, locks can be acquired only after notification in the
      // unlock, avoiding any unnecessary spinning.
      // Note that even though wait guarantees it returns only after the value
      // has changed, the lock is acquired after the next condition check.
      m_.wait(true, std::memory_order_relaxed)
#endif
          ;
  }
  bool try_lock() noexcept {
    return !m_.test_and_set(std::memory_order_acquire);
  }
  void unlock() noexcept {
    m_.clear(std::memory_order_release);
#if defined(__cpp_lib_atomic_wait) && __cpp_lib_atomic_wait >= 201907L
    m_.notify_one();
#endif
  }
};
static mutex m;

static int out{};

void f(std::size_t n) {
  for (std::size_t cnt{}; cnt < 40; ++cnt) {
    std::lock_guard lock{m};
    std::cout << n << ((++out % 40) == 0 ? '\n' : ' ');
  }
}

void run() {
  out = 0;
  std::vector<std::thread> v;
  for (std::size_t n{}; n < 10; ++n) {
    v.emplace_back(f, n);
  }
  for (auto &t : v) {
    t.join();
  }
}
} // namespace atomic_flag_test
