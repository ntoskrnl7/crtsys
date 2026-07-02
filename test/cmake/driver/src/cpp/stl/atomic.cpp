//
// https://en.cppreference.com/w/cpp/atomic/atomic#Example
//
#include <atomic>
#include <chrono>
#include <cassert>
#include <functional>
#include <iostream>
#include <mutex>
#include <numeric>
#include <stdexcept>
#include <string>
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
#if defined(__cpp_lib_atomic_ref) && __cpp_lib_atomic_ref >= 201806L &&        \
    (!defined(_MSC_VER) || _MSC_VER >= 1930)
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

//
// https://en.cppreference.com/w/cpp/atomic/atomic_thread_fence#Example
//
namespace atomic_thread_fence_test {
constexpr int num_mailboxes = 32;
std::atomic<int> mailbox_receiver[num_mailboxes];
std::string mailbox_data[num_mailboxes];

void run() {
  constexpr int my_id = 7;
  for (int i = 0; i < num_mailboxes; ++i) {
    mailbox_receiver[i].store(-1, std::memory_order_relaxed);
    mailbox_data[i].clear();
  }

  // cppreference presents this as a snippet with "..." placeholders. The
  // driver test supplies concrete mailbox data so the fence path is executable.
  mailbox_data[11] = "fence-protected message";
  std::atomic_store_explicit(&mailbox_receiver[11], my_id,
                             std::memory_order_release);

  bool observed = false;
  for (int i = 0; i < num_mailboxes; ++i) {
    if (std::atomic_load_explicit(&mailbox_receiver[i],
                                  std::memory_order_relaxed) == my_id) {
      // synchronize with just one writer
      std::atomic_thread_fence(std::memory_order_acquire);
      // guaranteed to observe everything done in the writer thread
      // before the atomic_store_explicit()
      std::cout << mailbox_data[i] << '\n';
      observed = mailbox_data[i] == "fence-protected message";
    }
  }
  assert(observed);
}
} // namespace atomic_thread_fence_test

//
// https://en.cppreference.com/w/cpp/atomic/atomic_signal_fence
//
namespace atomic_signal_fence_test {
void run() {
  // cppreference documents atomic_signal_fence as an API page without a
  // standalone runnable example; keep direct API coverage here.
  std::atomic_signal_fence(std::memory_order_seq_cst);
  std::atomic_signal_fence(std::memory_order_acquire);
  std::cout << "atomic_signal_fence calls completed\n";
}
} // namespace atomic_signal_fence_test

//
// https://en.cppreference.com/w/cpp/atomic/atomic_fetch_add#Example
//
namespace atomic_fetch_add_test {
using namespace std::chrono_literals;
// meaning of cnt:
//  5: readers and writer are in race. There are no active readers or writers.
//  4...0: there are 1...5 active readers, The writer is blocked.
// -1: writer won the race and readers are blocked.

const int N = 5; // four concurrent readers are allowed
std::atomic<int> cnt(N);

std::vector<int> data;
void reader(int id) {
  for (;;) {
    // lock
    while (std::atomic_fetch_sub(&cnt, 1) <= 0) {
      std::atomic_fetch_add(&cnt, 1);
    }

    // read
    if (!data.empty()) {
      std::cout << ("reader " + std::to_string(id) + " sees " +
                    std::to_string(*data.rbegin()) + '\n');
    }
    if (data.size() == 25) {
      break;
    }

    // unlock
    std::atomic_fetch_add(&cnt, 1);
    // pause
    std::this_thread::sleep_for(1ms);
  }
}

void writer() {
  for (int n = 0; n < 25; ++n) {
    // lock
    while (std::atomic_fetch_sub(&cnt, N + 1) != N) {
      std::atomic_fetch_add(&cnt, N + 1);
    }

    // write
    data.push_back(n);
    std::cout << "writer pushed back " << n << '\n';

    // unlock
    std::atomic_fetch_add(&cnt, N + 1);
    // pause
    std::this_thread::sleep_for(1ms);
  }
}

void run() {
  cnt = N;
  data.clear();

  std::vector<std::thread> v;
  for (int n = 0; n < N; ++n) {
    v.emplace_back(reader, n);
  }
  v.emplace_back(writer);

  for (auto &t : v) {
    t.join();
  }

  if (data.size() != 25) {
    throw std::runtime_error("unexpected atomic_fetch_add data size");
  }
}
} // namespace atomic_fetch_add_test

//
// https://en.cppreference.com/w/cpp/atomic/atomic_compare_exchange#Example
//
namespace atomic_compare_exchange_test {
template <class T> struct node {
  T data;
  node *next;
  node(const T &data) : data(data), next(nullptr) {}
};

template <class T> class stack {
  std::atomic<node<T> *> head{nullptr};

public:
  ~stack() {
    node<T> *current = head.load(std::memory_order_relaxed);
    while (current != nullptr) {
      node<T> *next = current->next;
      delete current;
      current = next;
    }
  }

  void push(const T &data) {
    node<T> *new_node = new node<T>(data);
    // put the current value of head into new_node->next
    new_node->next = head.load(std::memory_order_relaxed);
    // now make new_node the new head, but if the head
    // is no longer what's stored in new_node->next
    // (some other thread must have inserted a node just now)
    // then put that new head into new_node->next and try again
    while (!std::atomic_compare_exchange_weak_explicit(
        &head, &new_node->next, new_node, std::memory_order_release,
        std::memory_order_relaxed)) {
      ; // the body of the loop is empty
    }
    // note: the above loop is not thread-safe in at least
    // GCC prior to 4.8.3 (bug 60272), clang prior to 2014-05-05 (bug 18899)
    // MSVC prior to 2014-03-17 (bug 819819). See member function version for
    // workaround
  }
};

void run() {
  // cppreference's minimal program exits immediately after main(); the driver
  // harness initializes head explicitly and frees the nodes before unload.
  stack<int> s;
  s.push(1);
  s.push(2);
  s.push(3);
}
} // namespace atomic_compare_exchange_test
