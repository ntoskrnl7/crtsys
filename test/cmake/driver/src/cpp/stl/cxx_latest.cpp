#include <algorithm>
#include <array>
#include <cstdint>
#include <iostream>
#include <numeric>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>
#include <version>

#if __has_include(<coroutine>)
#include <coroutine>
#endif
#if __has_include(<execution>)
#include <execution>
#endif
#if __has_include(<flat_map>)
#include <flat_map>
#endif
#if __has_include(<flat_set>)
#include <flat_set>
#endif
#if __has_include(<generator>)
#include <generator>
#endif
#if __has_include(<mdspan>)
#include <mdspan>
#endif
#if __has_include(<print>)
#include <print>
#endif
#if __has_include(<ranges>)
#include <ranges>
#endif
#if __has_include(<stacktrace>)
#include <stacktrace>
#endif

namespace cxx_latest_detail {
void expect(bool condition, const char *message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}
} // namespace cxx_latest_detail

//
// https://en.cppreference.com/w/cpp/coroutine/noop_coroutine#Example
//
namespace coroutine_noop_test {
#if defined(__cpp_lib_coroutine) && __cpp_lib_coroutine >= 201902L
template <class T> struct task {
  struct promise_type {
    auto get_return_object() {
      return task{
          std::coroutine_handle<promise_type>::from_promise(*this)};
    }

    std::suspend_always initial_suspend() noexcept { return {}; }

    struct final_awaiter {
      bool await_ready() noexcept { return false; }
      void await_resume() noexcept {}
      std::coroutine_handle<>
      await_suspend(std::coroutine_handle<promise_type> handle) noexcept {
        if (auto previous_handle = handle.promise().previous) {
          return previous_handle;
        }

        return std::noop_coroutine();
      }
    };

    final_awaiter final_suspend() noexcept { return {}; }
    void unhandled_exception() { throw; }
    void return_value(T value) { result = std::move(value); }

    T result{};
    std::coroutine_handle<> previous{};
  };

  explicit task(std::coroutine_handle<promise_type> handle) : coro(handle) {}
  task(const task &) = delete;
  task &operator=(const task &) = delete;
  task(task &&) = delete;
  task &operator=(task &&) = delete;

  ~task() { coro.destroy(); }

  struct awaiter {
    bool await_ready() noexcept { return false; }
    T await_resume() { return std::move(coro.promise().result); }
    auto await_suspend(std::coroutine_handle<> handle) noexcept {
      coro.promise().previous = handle;
      return coro;
    }

    std::coroutine_handle<promise_type> coro;
  };

  awaiter operator co_await() { return awaiter{coro}; }

  T operator()() {
    coro.resume();
    return std::move(coro.promise().result);
  }

private:
  std::coroutine_handle<promise_type> coro;
};

task<int> get_random() {
  std::cout << "in get_random()\n";
  co_return 4;
}

task<int> test() {
  task<int> v = get_random();
  task<int> u = get_random();

  std::cout << "in test()\n";

  int x = co_await v + co_await u;
  co_return x;
}

void run() {
  task<int> t = test();
  const int result = t();
  std::cout << result << '\n';
  cxx_latest_detail::expect(result == 8,
                            "noop_coroutine example returned wrong result");
}
#else
void run() { std::cout << "std::coroutine is not available in this STL\n"; }
#endif
} // namespace coroutine_noop_test

//
// https://en.cppreference.com/w/cpp/coroutine/generator#Example
//
namespace generator_test {
#if defined(__cpp_lib_generator) && __cpp_lib_generator >= 202207L
template <typename T> struct Tree {
  T value;
  Tree *left{};
  Tree *right{};

  std::generator<const T &> traverse_inorder() const {
    if (left) {
      co_yield std::ranges::elements_of(left->traverse_inorder());
    }

    co_yield value;

    if (right) {
      co_yield std::ranges::elements_of(right->traverse_inorder());
    }
  }
};

void run() {
  Tree<char> a{'A'};
  Tree<char> c{'C'};
  Tree<char> e{'E'};
  Tree<char> g{'G'};
  Tree<char> b{'B', &a, &c};
  Tree<char> f{'F', &e, &g};
  Tree<char> d{'D', &b, &f};

  std::string traversal;
  for (const char value : d.traverse_inorder()) {
    traversal.push_back(value);
    traversal.push_back(' ');
    std::cout << value << ' ';
  }
  std::cout << '\n';

  cxx_latest_detail::expect(traversal == "A B C D E F G ",
                            "generator inorder traversal mismatch");
}
#else
void run() { std::cout << "std::generator is not available in this STL\n"; }
#endif
} // namespace generator_test

//
// https://en.cppreference.com/w/cpp/container/mdspan#Example
//
namespace mdspan_test {
#if defined(__cpp_lib_mdspan) && __cpp_lib_mdspan >= 202207L
void run() {
  std::vector v{1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12};

  auto ms2 = std::mdspan(v.data(), 2, 6);
  auto ms3 = std::mdspan(v.data(), 2, 3, 2);

  for (std::size_t i = 0; i != ms2.extent(0); ++i) {
    for (std::size_t j = 0; j != ms2.extent(1); ++j) {
      ms2[i, j] = static_cast<int>(i * 1000 + j);
    }
  }

  int sum = 0;
  for (std::size_t i = 0; i != ms3.extent(0); ++i) {
    for (std::size_t j = 0; j != ms3.extent(1); ++j) {
      for (std::size_t k = 0; k != ms3.extent(2); ++k) {
#if defined(__cpp_lib_print) && __cpp_lib_print >= 202207L
        std::print("ms3[{}, {}, {}] == {}\n", i, j, k, ms3[i, j, k]);
#else
        // Older MSVC STL toolsets can expose mdspan before std::print. Keep
        // the cppreference mdspan operations and only fall back for output.
        std::cout << "ms3[" << i << ", " << j << ", " << k
                  << "] == " << ms3[i, j, k] << '\n';
#endif
        sum += ms3[i, j, k];
      }
    }
  }

  cxx_latest_detail::expect(ms3[1, 2, 1] == 1005,
                            "mdspan 2D/3D mapping mismatch");
  cxx_latest_detail::expect(sum == 6030, "mdspan value sum mismatch");
}
#else
void run() { std::cout << "std::mdspan is not available in this STL\n"; }
#endif
} // namespace mdspan_test

//
// https://en.cppreference.com/w/cpp/utility/basic_stacktrace#Example
//
namespace stacktrace_test {
#if defined(__cpp_lib_stacktrace) && __cpp_lib_stacktrace >= 202011L
int nested_func(int c) {
  std::cout << std::stacktrace::current() << '\n';
  return c + 1;
}

int func(int b) { return nested_func(b + 1); }

void run() {
  const int result = func(777);
  std::cout << result << '\n';
  cxx_latest_detail::expect(result == 779,
                            "stacktrace example returned wrong result");
}
#else
void run() { std::cout << "std::stacktrace is not available in this STL\n"; }
#endif
} // namespace stacktrace_test

namespace stacktrace_semantic_test {
#if defined(__cpp_lib_stacktrace) && __cpp_lib_stacktrace >= 202011L
std::stacktrace capture_trace() {
  auto trace = std::stacktrace::current();
  cxx_latest_detail::expect(!trace.empty(),
                            "std::stacktrace::current returned no frames");
  return trace;
}

void run() {
  auto trace = capture_trace();
  std::cout << "stacktrace frames: " << trace.size() << '\n';
  const auto trace_text = std::to_string(trace);
  std::cout << "stacktrace text size: " << trace_text.size() << '\n';
  const auto &entry = trace[0];
  std::cout << "stacktrace entry description: " << entry.description() << '\n';
  std::cout << "stacktrace entry source_file: " << entry.source_file() << '\n';
  std::cout << "stacktrace entry source_line: " << entry.source_line() << '\n';
  std::cout << "stacktrace first entry: " << std::to_string(trace[0])
            << '\n';
  std::cout << "stacktrace last entry: "
            << std::to_string(trace[trace.size() - 1]) << '\n';

  const auto description = entry.description();
  cxx_latest_detail::expect(description.find("+0x") != std::string::npos,
                            "stacktrace description missing module offset");
  cxx_latest_detail::expect(std::to_string(entry).find("+0x") !=
                                std::string::npos,
                            "stacktrace entry string missing module offset");
  cxx_latest_detail::expect(trace_text.find("0> ") != std::string::npos,
                            "stacktrace text missing first frame");
  cxx_latest_detail::expect(trace_text.find("+0x") != std::string::npos,
                            "stacktrace text missing module offset");
  if (trace.size() > 1) {
    const auto last_prefix =
        std::to_string(static_cast<unsigned long long>(trace.size() - 1)) +
        "> ";
    cxx_latest_detail::expect(trace_text.find(last_prefix) != std::string::npos,
                              "stacktrace text missing last frame");
  }
}
#else
void run() { std::cout << "std::stacktrace is not available in this STL\n"; }
#endif
} // namespace stacktrace_semantic_test

//
// https://en.cppreference.com/w/cpp/algorithm/execution_policy_tag#Example
//
namespace execution_policy_test {
#if defined(__cpp_lib_execution)
std::vector<std::uint64_t> make_input() {
  std::vector<std::uint64_t> values(4096);
  std::uint64_t state = 0x12345678ULL;
  std::generate(values.begin(), values.end(), [&state] {
    state = state * 6364136223846793005ULL + 1442695040888963407ULL;
    return state;
  });
  return values;
}

template <class Policy>
void sort_and_check(Policy policy, const char *name,
                    const std::vector<std::uint64_t> &input) {
  auto values = input;
  std::sort(policy, values.begin(), values.end());
  cxx_latest_detail::expect(std::is_sorted(values.begin(), values.end()),
                            "execution-policy sort did not sort");
  std::cout << name << " sorted " << values.size() << " values\n";
}

void run() {
  const auto input = make_input();

  // The cppreference page uses a million random values for a timing demo.
  // Keep the execution-policy calls, but use deterministic smaller input so
  // the kernel driver test does not become a benchmark or a large allocator
  // stress test.
  sort_and_check(std::execution::seq, "seq", input);
  sort_and_check(std::execution::unseq, "unseq", input);
  sort_and_check(std::execution::par, "par", input);
  sort_and_check(std::execution::par_unseq, "par_unseq", input);
}
#else
void run() { std::cout << "std::execution is not available in this STL\n"; }
#endif
} // namespace execution_policy_test

//
// https://en.cppreference.com/w/cpp/container/flat_map
//
// The cppreference flat_map page currently has no runnable Example section, so
// this harness uses a small direct API check when the active STL exposes it.
//
namespace flat_map_test {
#if defined(__cpp_lib_flat_map)
void run() {
  std::flat_map<int, std::string> values;
  values.emplace(2, "two");
  values.emplace(1, "one");
  values[3] = "three";

  cxx_latest_detail::expect(values.contains(1),
                            "flat_map missing inserted key");
  cxx_latest_detail::expect(values.find(2)->second == "two",
                            "flat_map find returned wrong value");
  cxx_latest_detail::expect(values.size() == 3,
                            "flat_map size mismatch");
  std::cout << "flat_map size = " << values.size() << '\n';
}
#else
void run() { std::cout << "std::flat_map is not available in this STL\n"; }
#endif
} // namespace flat_map_test

//
// https://en.cppreference.com/w/cpp/container/flat_set
//
// The cppreference flat_set page currently has no runnable Example section, so
// this harness uses a small direct API check when the active STL exposes it.
//
namespace flat_set_test {
#if defined(__cpp_lib_flat_set)
void run() {
  std::flat_set<int> values{3, 1, 2};
  const auto [where, inserted] = values.insert(4);

  cxx_latest_detail::expect(inserted && *where == 4,
                            "flat_set insert failed");
  cxx_latest_detail::expect(values.contains(2),
                            "flat_set missing inserted value");
  cxx_latest_detail::expect(values.size() == 4, "flat_set size mismatch");
  std::cout << "flat_set size = " << values.size() << '\n';
}
#else
void run() { std::cout << "std::flat_set is not available in this STL\n"; }
#endif
} // namespace flat_set_test
