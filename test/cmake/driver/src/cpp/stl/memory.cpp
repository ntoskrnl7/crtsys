//
// https://en.cppreference.com/w/cpp/memory/shared_ptr#Example
//
#include <array>
#include <chrono>
#include <cassert>
#include <cstdio>
#include <cstddef>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <list>
#include <locale>
#include <memory>
#include <memory_resource>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <vector>

using namespace std::chrono_literals;

namespace memory_test_detail {
void expect(bool condition, const char *message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}
} // namespace memory_test_detail

namespace shared_ptr_test {
struct Base {
  Base() { std::cout << "Base::Base()\n"; }

  // Note: non-virtual destructor is OK here
  ~Base() { std::cout << "Base::~Base()\n"; }
};

struct Derived : public Base {
  Derived() { std::cout << "Derived::Derived()\n"; }
  ~Derived() { std::cout << "Derived::~Derived()\n"; }
};

void print(auto rem, std::shared_ptr<Base> const &sp) {
  std::cout << rem << "\n\t get() = " << sp.get()
            << ", use_count() = " << sp.use_count() << '\n';
}

void thr(std::shared_ptr<Base> p) {
  std::this_thread::sleep_for(987ms);
  std::shared_ptr<Base> lp = p; // thread-safe, even though the
                                // shared use_count is incremented
  {
    static std::mutex io_mutex;
    std::lock_guard<std::mutex> lk(io_mutex);
    print("Local pointer in a thread:", lp);
  }
}

void run() {
  std::shared_ptr<Base> p = std::make_shared<Derived>();
  print("Created a shared Derived (as a pointer to Base)", p);

  std::thread t1{thr, p}, t2{thr, p}, t3{thr, p};
  p.reset(); // release ownership from main

  print("Shared ownership between 3 threads and released ownership from main:",
        p);

  t1.join();
  t2.join();
  t3.join();

  std::cout << "All threads completed, the last one deleted Derived.\n";
}
} // namespace shared_ptr_test

//
// https://en.cppreference.com/w/cpp/memory/weak_ptr#Example
//
namespace weak_ptr_test {
std::weak_ptr<int> gw;

void observe() {
  std::cout << "gw.use_count() == " << gw.use_count() << "; ";
  // we have to make a copy of shared pointer before usage:
  if (std::shared_ptr<int> spt = gw.lock()) {
    std::cout << "*spt == " << *spt << '\n';
  } else {
    std::cout << "gw is expired\n";
  }
}

void run() {
  {
    auto sp = std::make_shared<int>(42);
    gw = sp;

    observe();
  }

  observe();
}
} // namespace weak_ptr_test

//
// https://en.cppreference.com/w/cpp/memory/monotonic_buffer_resource#Example
//
namespace pmr_monotonic_buffer_resource_test {
template <typename Func> auto benchmark(Func test_func, int iterations) {
  const auto start = std::chrono::system_clock::now();
  while (iterations-- > 0) {
    test_func();
  }
  const auto stop = std::chrono::system_clock::now();
  const auto secs = std::chrono::duration<double>(stop - start);
  return secs.count();
}

void run() {
  constexpr int iterations{100};
  constexpr int total_nodes{2'00'000};

  auto default_std_alloc = [total_nodes] {
    std::list<int> list;
    for (int i{}; i != total_nodes; ++i) {
      list.push_back(i);
    }
  };

  auto default_pmr_alloc = [total_nodes] {
    std::pmr::list<int> list;
    for (int i{}; i != total_nodes; ++i) {
      list.push_back(i);
    }
  };

  auto pmr_alloc_no_buf = [total_nodes] {
    std::pmr::monotonic_buffer_resource mbr;
    std::pmr::polymorphic_allocator<int> pa{&mbr};
    std::pmr::list<int> list{pa};
    for (int i{}; i != total_nodes; ++i) {
      list.push_back(i);
    }
  };

  const auto buffer_size = static_cast<std::size_t>(total_nodes) * 32;
  auto buffer = std::unique_ptr<std::byte[]>{new std::byte[buffer_size]};

  auto pmr_alloc_and_buf = [total_nodes, buffer = buffer.get(), buffer_size] {
    // cppreference uses a local stack buffer here:
    // std::array<std::byte, total_nodes * 32> buffer; // enough to fit in all nodes
    // std::pmr::monotonic_buffer_resource mbr{buffer.data(), buffer.size()};
    std::pmr::monotonic_buffer_resource mbr{buffer, buffer_size};
    std::pmr::polymorphic_allocator<int> pa{&mbr};
    std::pmr::list<int> list{pa};
    for (int i{}; i != total_nodes; ++i) {
      list.push_back(i);
    }
  };

  const double t1 = benchmark(default_std_alloc, iterations);
  const double t2 = benchmark(default_pmr_alloc, iterations);
  const double t3 = benchmark(pmr_alloc_no_buf, iterations);
  const double t4 = benchmark(pmr_alloc_and_buf, iterations);

  std::cout << std::fixed << std::setprecision(3)
            << "t1 (default std alloc): " << t1 << " sec; t1/t1: " << t1 / t1
            << '\n'
            << "t2 (default pmr alloc): " << t2 << " sec; t1/t2: " << t1 / t2
            << '\n'
            << "t3 (pmr alloc  no buf): " << t3 << " sec; t1/t3: " << t1 / t3
            << '\n'
            << "t4 (pmr alloc and buf): " << t4 << " sec; t1/t4: " << t1 / t4
            << '\n';
}
} // namespace pmr_monotonic_buffer_resource_test

//
// https://en.cppreference.com/w/cpp/memory/unsynchronized_pool_resource
// https://en.cppreference.com/w/cpp/memory/synchronized_pool_resource
// https://en.cppreference.com/w/cpp/memory/polymorphic_allocator
// https://en.cppreference.com/w/cpp/memory/null_memory_resource
// https://en.cppreference.com/w/cpp/memory/new_delete_resource
//
namespace pmr_pool_resource_test {
void run() {
  // These cppreference API pages do not provide standalone "Run this code"
  // examples. This compact driver check follows the documented API surface:
  // pool_options, upstream_resource(), release(), polymorphic_allocator
  // allocate/deallocate, and the standard singleton resources.
  std::pmr::pool_options options{};
  options.max_blocks_per_chunk = 4;
  options.largest_required_pool_block = 128;

  std::pmr::unsynchronized_pool_resource unsync_pool{
      options, std::pmr::new_delete_resource()};
  memory_test_detail::expect(unsync_pool.upstream_resource() ==
                                 std::pmr::new_delete_resource(),
                             "unsynchronized_pool_resource upstream mismatch");
  memory_test_detail::expect(unsync_pool.options().max_blocks_per_chunk == 4,
                             "unsynchronized_pool_resource option mismatch");

  std::pmr::vector<int> values{&unsync_pool};
  for (int i{}; i != 32; ++i) {
    values.push_back(i);
  }
  memory_test_detail::expect(values.size() == 32,
                             "unsynchronized_pool_resource vector size");
  memory_test_detail::expect(values.front() == 0 && values.back() == 31,
                             "unsynchronized_pool_resource vector values");
  values.clear();
  values.shrink_to_fit();
  unsync_pool.release();

  std::pmr::polymorphic_allocator<int> allocator{&unsync_pool};
  int *raw = allocator.allocate(3);
  std::allocator_traits<decltype(allocator)>::construct(allocator, raw, 7);
  std::allocator_traits<decltype(allocator)>::construct(allocator, raw + 1, 8);
  std::allocator_traits<decltype(allocator)>::construct(allocator, raw + 2, 9);
  memory_test_detail::expect(raw[0] == 7 && raw[1] == 8 && raw[2] == 9,
                             "polymorphic_allocator values mismatch");
  std::allocator_traits<decltype(allocator)>::destroy(allocator, raw + 2);
  std::allocator_traits<decltype(allocator)>::destroy(allocator, raw + 1);
  std::allocator_traits<decltype(allocator)>::destroy(allocator, raw);
  allocator.deallocate(raw, 3);

  void *direct = std::pmr::new_delete_resource()->allocate(64, alignof(int));
  std::pmr::new_delete_resource()->deallocate(direct, 64, alignof(int));

  try {
    [[maybe_unused]] void *never =
        std::pmr::null_memory_resource()->allocate(1, alignof(int));
    memory_test_detail::expect(false, "null_memory_resource did not throw");
  } catch (const std::bad_alloc &) {
  }

  std::pmr::synchronized_pool_resource sync_pool{
      options, std::pmr::new_delete_resource()};
  memory_test_detail::expect(sync_pool.upstream_resource() ==
                                 std::pmr::new_delete_resource(),
                             "synchronized_pool_resource upstream mismatch");

  auto worker = [&sync_pool](int base) {
    std::pmr::vector<int> local{&sync_pool};
    for (int i{}; i != 16; ++i) {
      local.push_back(base + i);
    }
    memory_test_detail::expect(local.size() == 16,
                               "synchronized_pool_resource vector size");
    memory_test_detail::expect(local.front() == base &&
                                   local.back() == base + 15,
                               "synchronized_pool_resource vector values");
  };

  std::thread t1{worker, 100};
  std::thread t2{worker, 200};
  t1.join();
  t2.join();
  sync_pool.release();
}
} // namespace pmr_pool_resource_test

//
// https://en.cppreference.com/w/cpp/memory/allocator_traits
//
namespace allocator_traits_test {
void run() {
  // This cppreference API page has no standalone "Run this code" example.
  // Exercise the allocator_traits member aliases and allocate/construct/
  // destroy/deallocate forwarding path directly.
  using allocator_type = std::allocator<int>;
  using traits = std::allocator_traits<allocator_type>;
  static_assert(std::is_same_v<traits::value_type, int>);
  static_assert(std::is_same_v<traits::pointer, int *>);

  allocator_type allocator;
  int *raw = traits::allocate(allocator, 2);
  traits::construct(allocator, raw, 47);
  traits::construct(allocator, raw + 1, 53);
  memory_test_detail::expect(raw[0] == 47 && raw[1] == 53,
                             "allocator_traits values mismatch");
  traits::destroy(allocator, raw + 1);
  traits::destroy(allocator, raw);
  traits::deallocate(allocator, raw, 2);
}
} // namespace allocator_traits_test

//
// https://en.cppreference.com/w/cpp/memory/unique_ptr#Example
//
namespace unique_ptr_test {
// helper class for runtime polymorphism demo below
struct B {
  virtual ~B() = default;

  virtual void bar() { std::cout << "B::bar\n"; }
};

struct D : B {
  D() { std::cout << "D::D\n"; }
  ~D() { std::cout << "D::~D\n"; }
  void bar() override { std::cout << "D::bar\n"; }
};

// a function consuming a unique_ptr can take it by value or by rvalue reference
std::unique_ptr<D> pass_through(std::unique_ptr<D> p) {
  p->bar();
  return p;
}

// helper function for the custom deleter demo below
void close_file(std::FILE *fp) { std::fclose(fp); }

// unique_ptr-based linked list demo
struct List {
  struct Node {
    int data;
    std::unique_ptr<Node> next;
  };

  std::unique_ptr<Node> head;
  ~List() {
    // destroy list nodes sequentially in a loop, the default destructor
    // would have invoked its "next"'s destructor recursively, which would
    // cause stack overflow for sufficiently large lists.
    while (head) {
      auto next = std::move(head->next);
      head = std::move(next);
    }
  }
  void push(int data) {
    head = std::unique_ptr<Node>(new Node{data, std::move(head)});
  }
};

void run() {
  std::cout << "1) Unique ownership semantics demo\n";
  {
    // Create a (uniquely owned) resource
    std::unique_ptr<D> p = std::make_unique<D>();
    // Transfer ownership to "pass_through",
    // which in turn transfers ownership back through the return value
    std::unique_ptr<D> q = pass_through(std::move(p));

    // "p" is now in a moved-from 'empty' state, equal to nullptr
    assert(!p);
  }

  std::cout << "\n"
               "2) Runtime polymorphism demo\n";
  {
    // Create a derived resource and point to it via base type
    std::unique_ptr<B> p = std::make_unique<D>();
    // Dynamic dispatch works as expected
    p->bar();
  }

  std::cout << "\n"
               "3) Custom deleter demo\n";
  std::ofstream("demo.txt") << 'x'; // prepare the file to read
  {
    using unique_file_t = std::unique_ptr<std::FILE, decltype(&close_file)>;
    unique_file_t fp(std::fopen("demo.txt", "r"), &close_file);
    if (fp) {
      std::cout << char(std::fgetc(fp.get())) << '\n';
    }
  } // "close_file()" called here (if "fp" is not null)

  std::cout << "\n"
               "4) Custom lambda expression deleter and exception safety "
               "demo\n";
  try {
    std::unique_ptr<D, void (*)(D *)> p(new D, [](D *ptr) {
      std::cout << "destroying from a custom deleter...\n";
      delete ptr;
    });
    throw std::runtime_error(""); // "p" would leak here if it were a plain pointer
  } catch (const std::exception &) {
    std::cout << "Caught exception\n";
  }

  std::cout << "\n"
               "5) Array form of unique_ptr demo\n";
  {
    std::unique_ptr<D[]> p(new D[3]);
  } // "D::~D()" is called 3 times

  std::cout << "\n"
               "6) Linked list demo\n";
  {
    List wall;
    const int enough{1'000'000};
    for (int beer = 0; beer != enough; ++beer) {
      wall.push(beer);
    }

    std::cout.imbue(std::locale("en_US.UTF-8"));
    std::cout << enough << " bottles of beer on the wall...\n";
  } // destroys all the beers
}
} // namespace unique_ptr_test
