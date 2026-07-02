//
// https://en.cppreference.com/w/cpp/memory/shared_ptr#Example
//
#if defined(_MSC_VER)
// Release builds define NDEBUG, so assert-only cppreference verification values
// can look unused under /WX. Keep the examples intact and silence those
// warnings only for this test translation unit.
#pragma warning(disable : 4101 4189)
#endif

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
#include <scoped_allocator>
#include <stdexcept>
#include <string>
#include <thread>
#include <tuple>
#include <unordered_map>
#include <vector>

using namespace std::chrono_literals;

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
// https://en.cppreference.com/w/cpp/memory/allocator_traits/construct#Example
//
namespace allocator_traits_construct_test {
struct foo {
  virtual int bar() { return 0; }
  virtual ~foo() {}
};

void run() {
  std::allocator<foo> al;
  using traits = std::allocator_traits<decltype(al)>;

  foo *p = traits::allocate(al, 1);
  traits::construct(al, p);
  p->bar();
  traits::destroy(al, p);
  traits::deallocate(al, p, 1);
}
} // namespace allocator_traits_construct_test

//
// https://en.cppreference.com/w/cpp/memory/null_memory_resource#Example
//
namespace pmr_null_memory_resource_test {
void run() {
  constexpr std::size_t buffer_size = 20000;
  // cppreference uses a stack buffer here:
  // std::array<std::byte, 20000> buf;
  // Kernel driver stack is small, so keep the same byte count in heap storage.
  auto buf = std::unique_ptr<std::byte[]>{new std::byte[buffer_size]};

  std::pmr::monotonic_buffer_resource pool{
      buf.get(), buffer_size, std::pmr::null_memory_resource()};

  std::pmr::unordered_map<long, std::pmr::string> coll{&pool};
  try {
    for (std::size_t i = 0; i != buffer_size; ++i) {
      coll.emplace(i, std::to_string(i));
      std::cout << "size: " << coll.size() << '\n';
    }
  } catch (const std::bad_alloc &e) {
    std::cerr << e.what() << '\n';
  }
}
} // namespace pmr_null_memory_resource_test

//
// https://en.cppreference.com/w/cpp/memory/polymorphic_allocator
//
namespace pmr_polymorphic_allocator_test {
void run() {
  // The cppreference polymorphic_allocator page documents the allocator API
  // without a standalone Example section. Exercise allocator propagation into
  // pmr containers with the public types shown on that page.
  std::pmr::monotonic_buffer_resource resource;
  std::pmr::polymorphic_allocator<std::pmr::string> allocator{&resource};
  std::pmr::vector<std::pmr::string> strings{allocator};

  strings.emplace_back("alpha");
  strings.emplace_back("beta");

  assert(strings.size() == 2);
  assert(strings[0] == "alpha");
  assert(strings[1] == "beta");
}
} // namespace pmr_polymorphic_allocator_test

//
// https://en.cppreference.com/w/cpp/memory/unsynchronized_pool_resource
// https://en.cppreference.com/w/cpp/memory/synchronized_pool_resource
// https://en.cppreference.com/w/cpp/memory/new_delete_resource
//
namespace pmr_pool_resource_test {
void exercise_pool(std::pmr::memory_resource *resource) {
  std::pmr::vector<int> values{resource};
  for (int i = 0; i < 16; ++i) {
    values.push_back(i);
  }
  assert(values.size() == 16);
  assert(values.back() == 15);
}

void run() {
  // The linked cppreference resource pages are API reference pages without
  // standalone examples. Cover the resource objects directly with pmr
  // containers and the documented upstream resource hook.
  std::pmr::unsynchronized_pool_resource unsynchronized{
      std::pmr::new_delete_resource()};
  exercise_pool(&unsynchronized);

  std::pmr::synchronized_pool_resource synchronized{
      std::pmr::new_delete_resource()};
  exercise_pool(&synchronized);

  exercise_pool(std::pmr::new_delete_resource());
}
} // namespace pmr_pool_resource_test

//
// https://en.cppreference.com/w/cpp/memory/allocator#Example
//
namespace allocator_test {
void run() {
  // Keep the cppreference allocator lifecycle: allocate, construct, use,
  // destroy, deallocate. The values are small to keep kernel test output short.
  std::allocator<int> allocator;

  int *p = allocator.allocate(4);
  for (int i = 0; i != 4; ++i) {
    std::allocator_traits<std::allocator<int>>::construct(allocator, p + i,
                                                          i * i);
  }

  for (int i = 0; i != 4; ++i) {
    std::cout << p[i] << ' ';
    std::allocator_traits<std::allocator<int>>::destroy(allocator, p + i);
  }
  std::cout << '\n';

  allocator.deallocate(p, 4);
}
} // namespace allocator_test

//
// https://en.cppreference.com/w/cpp/memory/uses_allocator#Example
// https://en.cppreference.com/w/cpp/memory/uses_allocator_construction_args#Example
// https://en.cppreference.com/w/cpp/memory/make_obj_using_allocator#Example
// https://en.cppreference.com/w/cpp/memory/uninitialized_construct_using_allocator#Example
//
namespace uses_allocator_test {
// The linked cppreference pages cover related allocator-aware construction
// helpers. Fold them into one tiny allocator-aware type so the driver validates
// the same construction dispatch without pulling in unrelated sample machinery.
struct AllocAware {
  using allocator_type = std::allocator<std::byte>;

  AllocAware(std::allocator_arg_t, allocator_type, int value)
      : value(value), used_allocator(true) {}

  explicit AllocAware(int value) : value(value), used_allocator(false) {}

  int value;
  bool used_allocator;
};

void run() {
  using allocator_type = std::allocator<std::byte>;
  static_assert(std::uses_allocator_v<AllocAware, allocator_type>);

#if defined(__cpp_lib_make_obj_using_allocator)
  // C++20 allocator construction helpers are not present in every supported
  // MSVC STL toolset, so keep the cppreference calls behind the feature macro.
  allocator_type allocator;

  auto args = std::uses_allocator_construction_args<AllocAware>(allocator, 42);
  const auto arg_count = std::tuple_size_v<decltype(args)>;
  assert(arg_count != 0);

  AllocAware object = std::make_obj_using_allocator<AllocAware>(allocator, 42);
  assert(object.value == 42);
  assert(object.used_allocator);

  alignas(AllocAware) std::byte storage[sizeof(AllocAware)];
  auto *p = reinterpret_cast<AllocAware *>(storage);
  std::uninitialized_construct_using_allocator(p, allocator, 7);
  assert(p->value == 7);
  assert(p->used_allocator);
  std::destroy_at(p);

  std::cout << "uses_allocator construction assertions passed\n";
#else
  std::cout << "allocator-aware construction helpers are not available in this "
               "MSVC STL\n";
#endif
}
} // namespace uses_allocator_test

//
// https://en.cppreference.com/w/cpp/memory/scoped_allocator_adaptor#Example
//
namespace scoped_allocator_adaptor_test {
void run() {
  // Compact the cppreference nested-container scenario to the allocator
  // propagation behavior that matters for driver coverage.
  using outer_allocator = std::allocator<std::vector<int>>;
  using inner_allocator = std::allocator<int>;
  using scoped_allocator =
      std::scoped_allocator_adaptor<outer_allocator, inner_allocator>;

  std::vector<std::vector<int>, scoped_allocator> v{scoped_allocator{}};
  v.emplace_back(3, 42);

  assert(v.size() == 1);
  assert(v.front().size() == 3);
  assert(v.front().front() == 42);
  std::cout << "scoped_allocator_adaptor assertions passed\n";
}
} // namespace scoped_allocator_adaptor_test

//
// https://en.cppreference.com/w/cpp/memory/enable_shared_from_this#Example
//
namespace enable_shared_from_this_test {
// cppreference also demonstrates incorrect construction. Keep only the valid
// shared_from_this path in the default driver test to avoid intentional throws.
struct Good : std::enable_shared_from_this<Good> {
  std::shared_ptr<Good> getptr() { return shared_from_this(); }
};

void run() {
  auto good = std::make_shared<Good>();
  std::shared_ptr<Good> gp1 = good->getptr();
  std::shared_ptr<Good> gp2 = good->getptr();

  assert(gp1.use_count() == 3);
  assert(gp2.use_count() == 3);
  std::cout << "enable_shared_from_this use_count: " << gp1.use_count()
            << '\n';
}
} // namespace enable_shared_from_this_test

//
// https://en.cppreference.com/w/cpp/memory/owner_less#Example
//
namespace owner_less_test {
void run() {
  // Keep the cppreference ownership-comparison semantics, but use assertions
  // instead of printing an implementation-dependent ordering table.
  auto p1 = std::make_shared<int>(1);
  auto p2 = std::make_shared<int>(2);
  std::weak_ptr<int> wp1 = p1;
  std::weak_ptr<int> wp2 = p2;

  std::owner_less<void> owner_less;
  assert(!owner_less(p1, wp1));
  assert(!owner_less(wp1, p1));
  assert(owner_less(p1, wp2) != owner_less(wp2, p1));

  std::cout << "owner_less assertions passed\n";
}
} // namespace owner_less_test

//
// https://en.cppreference.com/w/cpp/memory/to_address#Example
// https://en.cppreference.com/w/cpp/memory/addressof#Example
// https://en.cppreference.com/w/cpp/memory/align#Example
// https://en.cppreference.com/w/cpp/memory/assume_aligned#Example
//
namespace pointer_utility_test {
struct NonAddressable {
  int value;
  NonAddressable *operator&() = delete;
};

void run() {
  // The linked utility pages are independent examples. This compact test keeps
  // each documented operation: raw-pointer address extraction, overloaded
  // operator& bypass, manual alignment, and assume_aligned when available.
  int value = 42;
  int *raw = &value;
#if defined(__cpp_lib_to_address)
  assert(std::to_address(raw) == raw);
#else
  assert(raw == &value);
  std::cout << "std::to_address is not available in this MSVC STL\n";
#endif

  NonAddressable object{7};
  assert(std::addressof(object)->value == 7);

  alignas(double) std::byte buffer[sizeof(double) + alignof(double)];
  void *ptr = buffer;
  std::size_t space = sizeof(buffer);
  void *aligned = std::align(alignof(double), sizeof(double), ptr, space);
  assert(aligned != nullptr);

  double *d = ::new (aligned) double(3.5);
#if defined(__cpp_lib_assume_aligned)
  double *assumed = std::assume_aligned<alignof(double)>(d);
  assert(assumed == d);
#endif
  assert(*d == 3.5);
  std::destroy_at(d);

  std::cout << "pointer utility assertions passed\n";
}
} // namespace pointer_utility_test

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
