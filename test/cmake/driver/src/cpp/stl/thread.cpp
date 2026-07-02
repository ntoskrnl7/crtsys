//
// https://en.cppreference.com/w/cpp/thread/condition_variable#Example
//
#include <cassert>
#include <condition_variable>
#include <iostream>
#include <mutex>
#include <string>
#include <thread>

namespace condition_variable_test {
std::mutex m;
std::condition_variable cv;
std::string data;
bool ready = false;
bool processed = false;

void worker_thread() {
  // Wait until main() sends data
  std::unique_lock lk(m);
  cv.wait(lk, [] { return ready; });

  // after the wait, we own the lock.
  std::cout << "Worker thread is processing data\n";
  data += " after processing";

  // Send data back to main()
  processed = true;
  std::cout << "Worker thread signals data processing completed\n";

  // Manual unlocking is done before notifying, to avoid waking up
  // the waiting thread only to block again (see notify_one for details)
  lk.unlock();
  cv.notify_one();
}

void run() {
  std::thread worker(worker_thread);

  data = "Example data";
  // send data to the worker thread
  {
    std::lock_guard lk(m);
    ready = true;
    std::cout << "main() signals data ready for processing\n";
  }
  cv.notify_one();

  // wait for the worker
  {
    std::unique_lock lk(m);
    cv.wait(lk, [] { return processed; });
  }
  std::cout << "Back in main(), data = " << data << '\n';

  worker.join();
}
} // namespace condition_variable_test

//
// https://en.cppreference.com/w/cpp/thread/condition_variable_any/wait#Example
//
#include <chrono>
#include <condition_variable>
#include <iostream>
#include <mutex>
#include <thread>

namespace condition_variable_any_test {
std::condition_variable_any cv;
std::mutex cv_m; // This mutex is used for three purposes:
                 // 1) to synchronize accesses to i
                 // 2) to synchronize accesses to std::cerr
                 // 3) for the condition variable cv
int i = 0;
int finished = 0;

void waits() {
  std::unique_lock<std::mutex> lk(cv_m);
  std::cerr << "Waiting... \n";
  cv.wait(lk, [] { return i == 1; });
  ++finished;
  std::cerr << "...finished waiting. i == 1\n";
}

void signals() {
  // cppreference sleeps for seconds; the driver harness keeps the same
  // notify-before-ready and notify-after-ready sequence with shorter waits.
  std::this_thread::sleep_for(std::chrono::milliseconds(10));
  {
    std::lock_guard<std::mutex> lk(cv_m);
    std::cerr << "Notifying...\n";
  }
  cv.notify_all();

  std::this_thread::sleep_for(std::chrono::milliseconds(10));
  {
    std::lock_guard<std::mutex> lk(cv_m);
    i = 1;
    std::cerr << "Notifying again...\n";
  }
  cv.notify_all();
}

void run() {
  {
    std::lock_guard<std::mutex> lk(cv_m);
    i = 0;
    finished = 0;
  }

  std::thread t1(waits), t2(waits), t3(waits), t4(signals);
  t1.join();
  t2.join();
  t3.join();
  t4.join();

  assert(finished == 3);
}
} // namespace condition_variable_any_test

//
// https://en.cppreference.com/w/cpp/thread/mutex#Example
//
#include <chrono>
#include <iostream>
#include <map>
#include <mutex>
#include <string>
#include <thread>

namespace mutex_test {
std::map<std::string, std::string> g_pages;
std::mutex g_pages_mutex;

void save_page(const std::string &url) {
  // simulate a long page fetch
  std::this_thread::sleep_for(std::chrono::seconds(2));
  std::string result = "fake content";

  std::lock_guard<std::mutex> guard(g_pages_mutex);
  g_pages[url] = result;
}

void run() {
  std::thread t1(save_page, "http://foo");
  std::thread t2(save_page, "http://bar");
  t1.join();
  t2.join();

  // safe to access g_pages without lock now, as the threads are joined
  for (const auto &pair : g_pages) {
    std::cout << pair.first << " => " << pair.second << '\n';
  }
}
} // namespace mutex_test

//
// https://en.cppreference.com/w/cpp/thread/lock_guard#Example
//
#include <iostream>
#include <mutex>
#include <string_view>
#include <syncstream>
#include <thread>

namespace lock_guard_test {
volatile int g_i = 0;
std::mutex g_i_mutex; // protects g_i

void safe_increment(int iterations) {
  const std::lock_guard<std::mutex> lock(g_i_mutex);
  while (iterations-- > 0) {
    g_i = g_i + 1;
  }
  std::cout << "thread #" << std::this_thread::get_id() << ", g_i: " << g_i
            << '\n';

  // g_i_mutex is automatically released when lock goes out of scope
}

void unsafe_increment(int iterations) {
  while (iterations-- > 0) {
    g_i = g_i + 1;
  }
  std::osyncstream(std::cout)
      << "thread #" << std::this_thread::get_id() << ", g_i: " << g_i
      << '\n';
}

void run() {
  constexpr int iterations = 10'000;

  auto test = [=](std::string_view fun_name, auto fun) {
    g_i = 0;
    std::cout << fun_name << ":\n before, g_i: " << g_i << '\n';
    {
      // cppreference uses 1'000'000 iterations; keep the same safe/unsafe
      // increment shape with fewer iterations for the driver harness.
      std::jthread t1(fun, iterations);
      std::jthread t2(fun, iterations);
    }
    std::cout << "after, g_i: " << g_i << "\n\n";
  };

  test("safe_increment", safe_increment);
  assert(g_i == iterations * 2);
  test("unsafe_increment", unsafe_increment);
}
} // namespace lock_guard_test

//
// https://en.cppreference.com/w/cpp/thread/shared_mutex#Example
//
#include <iostream>
#include <mutex>
#include <shared_mutex>
#include <thread>

namespace shared_mutex_test {
class ThreadSafeCounter {
public:
  ThreadSafeCounter() = default;

  // Multiple threads/readers can read the counter's value at the same time.
  unsigned int get() const {
    std::shared_lock lock(mutex_);
    return value_;
  }

  // Only one thread/writer can increment/write the counter's value.
  unsigned int increment() {
    std::unique_lock lock(mutex_);
    return ++value_;
  }

  // Only one thread/writer can reset/write the counter's value.
  void reset() {
    std::unique_lock lock(mutex_);
    value_ = 0;
  }

private:
  mutable std::shared_mutex mutex_;
  unsigned int value_ = 0;
};

void run() {
  ThreadSafeCounter counter;

  auto increment_and_print = [&counter]() {
    for (int i = 0; i < 3; i++) {
      std::cout << std::this_thread::get_id() << ' ' << counter.increment()
                << '\n';

      // Note: Writing to std::cout actually needs to be synchronized as well
      // by another std::mutex. This has been omitted to keep the example small.
    }
  };

  std::thread thread1(increment_and_print);
  std::thread thread2(increment_and_print);

  thread1.join();
  thread2.join();
}
} // namespace shared_mutex_test

//
// https://en.cppreference.com/w/cpp/thread/shared_lock
//
namespace shared_lock_test {
// The cppreference shared_lock page has no standalone Example section; use a
// small direct coverage check for lock/unlock ownership semantics.
void run() {
  std::shared_mutex mutex;
  int value = 0;

  {
    std::unique_lock write_lock(mutex);
    value = 42;
  }

  std::shared_lock read_lock(mutex);
  assert(read_lock.owns_lock());
  assert(value == 42);
  read_lock.unlock();
  assert(!read_lock.owns_lock());
  read_lock.lock();
  assert(read_lock.owns_lock());
}
} // namespace shared_lock_test

//
// https://en.cppreference.com/w/cpp/thread/shared_timed_mutex#Example
//
namespace shared_timed_mutex_test {
class R {
  mutable std::shared_timed_mutex mut;
  int data = 0;

public:
  R() = default;
  explicit R(int value) : data(value) {}

  R &operator=(const R &other) {
    // requires exclusive ownership to write to *this
    std::unique_lock<std::shared_timed_mutex> lhs(mut, std::defer_lock);
    // requires shared ownership to read from other
    std::shared_lock<std::shared_timed_mutex> rhs(other.mut, std::defer_lock);
    std::lock(lhs, rhs);
    data = other.data;
    return *this;
  }

  int value() const {
    std::shared_lock<std::shared_timed_mutex> lock(mut);
    return data;
  }
};

void run() {
  R r;
  R other(42);
  // cppreference's page marks this Example as incomplete and its main() only
  // constructs R. Call assignment once so the demonstrated locking path is
  // actually covered by the driver test.
  r = other;
  assert(r.value() == 42);
}
} // namespace shared_timed_mutex_test

//
// https://en.cppreference.com/w/cpp/thread/shared_timed_mutex/try_lock_for
// https://en.cppreference.com/w/cpp/thread/shared_timed_mutex/try_lock_shared_for
// https://en.cppreference.com/w/cpp/thread/shared_timed_mutex/try_lock_shared_until
//
namespace shared_timed_mutex_timed_edge_test {
template <class LockAttempt, class Unlock>
bool eventually_locks(LockAttempt lock_attempt, Unlock unlock) {
  const auto deadline =
      std::chrono::steady_clock::now() + std::chrono::milliseconds(50);
  do {
    if (lock_attempt()) {
      unlock();
      return true;
    }
    std::this_thread::yield();
  } while (std::chrono::steady_clock::now() < deadline);
  return false;
}

void run() {
  std::shared_timed_mutex mutex;

  mutex.lock();
  bool shared_acquired_while_exclusive = true;
  std::thread shared_contender([&] {
    shared_acquired_while_exclusive =
        mutex.try_lock_shared_for(std::chrono::milliseconds(5));
    if (shared_acquired_while_exclusive) {
      mutex.unlock_shared();
    }
  });
  shared_contender.join();
  assert(!shared_acquired_while_exclusive);
  mutex.unlock();

  assert(eventually_locks(
      [&] {
        return mutex.try_lock_shared_until(std::chrono::steady_clock::now() +
                                           std::chrono::milliseconds(5));
      },
      [&] { mutex.unlock_shared(); }));

  mutex.lock_shared();
  bool exclusive_acquired_while_shared = true;
  std::thread exclusive_contender([&] {
    exclusive_acquired_while_shared =
        mutex.try_lock_for(std::chrono::milliseconds(5));
    if (exclusive_acquired_while_shared) {
      mutex.unlock();
    }
  });
  exclusive_contender.join();
  assert(!exclusive_acquired_while_shared);
  mutex.unlock_shared();

  assert(eventually_locks(
      [&] {
        return mutex.try_lock_until(std::chrono::steady_clock::now() +
                                    std::chrono::milliseconds(5));
      },
      [&] { mutex.unlock(); }));
}
} // namespace shared_timed_mutex_timed_edge_test

//
// https://en.cppreference.com/w/cpp/thread/scoped_lock#Example
//
#include <chrono>
#include <functional>
#include <iostream>
#include <mutex>
#include <string>
#include <syncstream>
#include <thread>
#include <vector>

namespace scoped_lock_test {
struct Employee {
  std::vector<std::string> lunch_partners;
  std::string id;
  std::mutex m;
  Employee(std::string id) : id(id) {}
  std::string partners() const {
    std::string ret = "Employee " + id + " has lunch partners: ";
    for (int count{}; const auto &partner : lunch_partners) {
      ret += (count++ ? ", " : "") + partner;
    }
    return ret;
  }
};

void send_mail(Employee &, Employee &) {
  // cppreference uses 1s to simulate messaging; keep the synchronization shape
  // but avoid making the driver test spend seconds in this example.
  std::this_thread::sleep_for(std::chrono::milliseconds(1));
}

void assign_lunch_partner(Employee &e1, Employee &e2) {
  std::osyncstream synced_out(std::cout);
  synced_out << e1.id << " and " << e2.id << " are waiting for locks"
             << std::endl;
  {
    // Use std::scoped_lock to acquire two locks without worrying about
    // other calls to assign_lunch_partner deadlocking us
    // and it also provides a convenient RAII-style mechanism

    std::scoped_lock lock(e1.m, e2.m);
    // Equivalent code 1 (using std::lock and std::lock_guard)
    // std::lock(e1.m, e2.m);
    // std::lock_guard<std::mutex> lk1(e1.m, std::adopt_lock);
    // std::lock_guard<std::mutex> lk2(e2.m, std::adopt_lock);
    // Equivalent code 2 (if unique_locks are needed, e.g. for condition
    // variables)
    // std::unique_lock<std::mutex> lk1(e1.m, std::defer_lock);
    // std::unique_lock<std::mutex> lk2(e2.m, std::defer_lock);
    // std::lock(lk1, lk2);
    synced_out << e1.id << " and " << e2.id << " got locks" << std::endl;
    e1.lunch_partners.push_back(e2.id);
    e2.lunch_partners.push_back(e1.id);
  }
  send_mail(e1, e2);
  send_mail(e2, e1);
}

void run() {
  Employee alice("Alice"), bob("Bob"), christina("Christina"), dave("Dave");
  // Assign in parallel threads because mailing users about lunch assignments
  // takes a long time
  std::vector<std::thread> threads;
  threads.emplace_back(assign_lunch_partner, std::ref(alice), std::ref(bob));
  threads.emplace_back(assign_lunch_partner, std::ref(christina),
                       std::ref(bob));
  threads.emplace_back(assign_lunch_partner, std::ref(christina),
                       std::ref(alice));
  threads.emplace_back(assign_lunch_partner, std::ref(dave), std::ref(bob));
  for (auto &thread : threads) {
    thread.join();
  }
  std::osyncstream(std::cout) << alice.partners() << '\n'
                              << bob.partners() << '\n'
                              << christina.partners() << '\n'
                              << dave.partners() << '\n';

  assert(alice.lunch_partners.size() == 2);
  assert(bob.lunch_partners.size() == 3);
  assert(christina.lunch_partners.size() == 2);
  assert(dave.lunch_partners.size() == 1);
}
} // namespace scoped_lock_test

//
// https://en.cppreference.com/w/cpp/thread/lock#Example
//
#include <chrono>
#include <functional>
#include <iostream>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace lock_test {
struct Employee {
  Employee(std::string id) : id(id) {}
  std::string id;
  std::vector<std::string> lunch_partners;
  std::mutex m;
  std::string output() const {
    std::string ret = "Employee " + id + " has lunch partners: ";
    for (auto n{lunch_partners.size()}; const auto &partner : lunch_partners) {
      ret += partner + (--n ? ", " : "");
    }
    return ret;
  }
};

void send_mail(Employee &, Employee &) {
  // cppreference uses 696ms to simulate messaging; keep the lock pattern but
  // shorten the delay for the driver harness.
  std::this_thread::sleep_for(std::chrono::milliseconds(1));
}

void assign_lunch_partner(Employee &e1, Employee &e2) {
  static std::mutex io_mutex;
  {
    std::lock_guard<std::mutex> lk(io_mutex);
    std::cout << e1.id << " and " << e2.id << " are waiting for locks"
              << std::endl;
  }
  // Use std::lock to acquire two locks without worrying about
  // other calls to assign_lunch_partner deadlocking us
  {
    std::lock(e1.m, e2.m);
    std::lock_guard<std::mutex> lk1(e1.m, std::adopt_lock);
    std::lock_guard<std::mutex> lk2(e2.m, std::adopt_lock);
    // Equivalent code (if unique_locks are needed, e.g. for condition
    // variables)
    //  std::unique_lock<std::mutex> lk1(e1.m, std::defer_lock);
    //  std::unique_lock<std::mutex> lk2(e2.m, std::defer_lock);
    //  std::lock(lk1, lk2);
    // Superior solution available in C++17
    //  std::scoped_lock lk(e1.m, e2.m);
    {
      std::lock_guard<std::mutex> lk(io_mutex);
      std::cout << e1.id << " and " << e2.id << " got locks" << std::endl;
    }
    e1.lunch_partners.push_back(e2.id);
    e2.lunch_partners.push_back(e1.id);
  }
  send_mail(e1, e2);
  send_mail(e2, e1);
}

void run() {
  Employee alice("Alice"), bob("Bob"), christina("Christina"), dave("Dave");
  // Assign in parallel threads because mailing users about lunch assignments
  // takes a long time
  std::vector<std::thread> threads;
  threads.emplace_back(assign_lunch_partner, std::ref(alice), std::ref(bob));
  threads.emplace_back(assign_lunch_partner, std::ref(christina),
                       std::ref(bob));
  threads.emplace_back(assign_lunch_partner, std::ref(christina),
                       std::ref(alice));
  threads.emplace_back(assign_lunch_partner, std::ref(dave), std::ref(bob));
  for (auto &thread : threads) {
    thread.join();
  }

  std::cout << alice.output() << '\n'
            << bob.output() << '\n'
            << christina.output() << '\n'
            << dave.output() << '\n';

  assert(alice.lunch_partners.size() == 2);
  assert(bob.lunch_partners.size() == 3);
  assert(christina.lunch_partners.size() == 2);
  assert(dave.lunch_partners.size() == 1);
}
} // namespace lock_test

//
// https://en.cppreference.com/w/cpp/thread/unique_lock#Example
//
#include <functional>
#include <iostream>
#include <mutex>
#include <thread>

namespace unique_lock_test {
struct Box {
  explicit Box(int num) : num_things{num} {}

  int num_things;
  std::mutex m;
};

void transfer(Box &from, Box &to, int num) {
  // don't actually take the locks yet
  std::unique_lock lock1{from.m, std::defer_lock};
  std::unique_lock lock2{to.m, std::defer_lock};
  // lock both unique_locks without deadlock
  std::lock(lock1, lock2);

  from.num_things -= num;
  to.num_things += num;

  // "from.m" and "to.m" mutexes unlocked in unique_lock dtors
}

void run() {
  Box acc1{100};
  Box acc2{50};

  std::thread t1{transfer, std::ref(acc1), std::ref(acc2), 10};
  std::thread t2{transfer, std::ref(acc2), std::ref(acc1), 5};

  t1.join();
  t2.join();
  std::cout << "acc1: " << acc1.num_things << "\n"
            << "acc2: " << acc2.num_things << '\n';

  assert(acc1.num_things == 95);
  assert(acc2.num_things == 55);
}
} // namespace unique_lock_test

//
// https://en.cppreference.com/w/cpp/thread/recursive_mutex#Example
//
#include <iostream>
#include <mutex>
#include <string>
#include <thread>

namespace recursive_mutex_test {
class X {
  std::recursive_mutex m;
  std::string shared;
  int fun1_count = 0;
  int fun2_count = 0;

public:
  void fun1() {
    std::lock_guard<std::recursive_mutex> lk(m);
    shared = "fun1";
    ++fun1_count;
    std::cout << "in fun1, shared variable is now " << shared << '\n';
  }

  void fun2() {
    std::lock_guard<std::recursive_mutex> lk(m);
    shared = "fun2";
    ++fun2_count;
    std::cout << "in fun2, shared variable is now " << shared << '\n';
    fun1(); // recursive lock becomes useful here
    std::cout << "back in fun2, shared variable is " << shared << '\n';
  }

  void verify() {
    std::lock_guard<std::recursive_mutex> lk(m);
    assert(fun1_count == 2);
    assert(fun2_count == 1);
  }
};

void run() {
  X x;
  std::thread t1(&X::fun1, &x);
  std::thread t2(&X::fun2, &x);
  t1.join();
  t2.join();
  x.verify();
}
} // namespace recursive_mutex_test

//
// https://en.cppreference.com/w/cpp/thread/timed_mutex
//
#include <chrono>
#include <mutex>
#include <thread>

namespace timed_mutex_test {
// The cppreference timed_mutex page has no standalone Example section; use a
// small direct coverage check for timed acquisition success/failure.
void run() {
  std::timed_mutex mutex;
  mutex.lock();

  bool acquired_while_owned = true;
  std::thread contender([&] {
    acquired_while_owned = mutex.try_lock_for(std::chrono::milliseconds(5));
    if (acquired_while_owned) {
      mutex.unlock();
    }
  });
  contender.join();
  assert(!acquired_while_owned);

  mutex.unlock();
  assert(mutex.try_lock_until(std::chrono::steady_clock::now() +
                              std::chrono::milliseconds(5)));
  mutex.unlock();
}
} // namespace timed_mutex_test

//
// https://en.cppreference.com/w/cpp/thread/recursive_timed_mutex
//
#include <chrono>
#include <mutex>
#include <thread>

namespace recursive_timed_mutex_test {
// The cppreference recursive_timed_mutex page has no standalone Example
// section; use a small direct coverage check for recursive ownership and timed
// acquisition failure from another thread.
void run() {
  std::recursive_timed_mutex mutex;
  mutex.lock();
  assert(mutex.try_lock());

  bool acquired_by_other_thread = true;
  std::thread contender([&] {
    acquired_by_other_thread =
        mutex.try_lock_for(std::chrono::milliseconds(5));
    if (acquired_by_other_thread) {
      mutex.unlock();
    }
  });
  contender.join();
  assert(!acquired_by_other_thread);

  mutex.unlock();
  mutex.unlock();
  assert(mutex.try_lock_until(std::chrono::steady_clock::now() +
                              std::chrono::milliseconds(5)));
  mutex.unlock();
}
} // namespace recursive_timed_mutex_test

//
// https://en.cppreference.com/w/cpp/thread/try_lock#Example
//
#include <chrono>
#include <functional>
#include <iostream>
#include <mutex>
#include <thread>
#include <vector>

namespace try_lock_test {
void run() {
  int foo_count = 0;
  std::mutex foo_count_mutex;
  int bar_count = 0;
  std::mutex bar_count_mutex;
  int overall_count = 0;
  bool done = false;
  std::mutex done_mutex;
  auto increment = [](int &counter, std::mutex &m, const char *desc) {
    for (int i = 0; i < 10; ++i) {
      std::unique_lock<std::mutex> lock(m);
      ++counter;
      std::cout << desc << ": " << counter << '\n';
      lock.unlock();
      // cppreference sleeps for seconds; this keeps the same interleaving
      // shape with milliseconds so the driver test remains quick.
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
  };
  std::thread increment_foo(increment, std::ref(foo_count),
                            std::ref(foo_count_mutex), "foo");
  std::thread increment_bar(increment, std::ref(bar_count),
                            std::ref(bar_count_mutex), "bar");
  std::thread update_overall([&]() {
    done_mutex.lock();
    while (!done) {
      done_mutex.unlock();
      int result = std::try_lock(foo_count_mutex, bar_count_mutex);
      if (result == -1) {
        overall_count += foo_count + bar_count;
        foo_count = 0;
        bar_count = 0;
        std::cout << "overall: " << overall_count << '\n';
        foo_count_mutex.unlock();
        bar_count_mutex.unlock();
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(2));
      done_mutex.lock();
    }
    done_mutex.unlock();
  });
  increment_foo.join();
  increment_bar.join();
  done_mutex.lock();
  done = true;
  done_mutex.unlock();
  update_overall.join();

  std::cout << "Done processing\n"
            << "foo: " << foo_count << '\n'
            << "bar: " << bar_count << '\n'
            << "overall: " << overall_count << '\n';

  assert(foo_count + bar_count + overall_count == 20);
}
} // namespace try_lock_test

//
// https://en.cppreference.com/w/cpp/thread/call_once#Example
//
#include <iostream>
#include <mutex>
#include <thread>

namespace call_once_test {
std::once_flag flag1, flag2;

void simple_do_once() {
  std::call_once(flag1,
                 []() { std::cout << "Simple example: called once\n"; });
}

void may_throw_function(bool do_throw) {
  if (do_throw) {
    std::cout << "Throw: call_once will retry\n";
    throw std::exception();
  }

  std::cout << "Did not throw, call_once will not attempt again\n";
}

void do_once(bool do_throw) {
  try {
    std::call_once(flag2, may_throw_function, do_throw);
  } catch (...) {
  }
}

void run() {
  std::thread st1(simple_do_once);
  std::thread st2(simple_do_once);
  std::thread st3(simple_do_once);
  std::thread st4(simple_do_once);
  st1.join();
  st2.join();
  st3.join();
  st4.join();

  std::thread t1(do_once, true);
  std::thread t2(do_once, true);
  std::thread t3(do_once, false);
  std::thread t4(do_once, true);
  t1.join();
  t2.join();
  t3.join();
  t4.join();
}
} // namespace call_once_test

//
// https://en.cppreference.com/w/cpp/thread/future#Example
//
#include <future>
#include <iostream>
#include <thread>

namespace future_test {
void run() {
  // future from a packaged_task
  std::packaged_task<int()> task([] { return 7; }); // wrap the function
  std::future<int> f1 = task.get_future();          // get a future
  std::thread t(std::move(task));                   // launch on a thread

  // future from an async()
  std::future<int> f2 = std::async(std::launch::async, [] { return 8; });

  // future from a promise
  std::promise<int> p;
  std::future<int> f3 = p.get_future();
  std::thread([&p] { p.set_value_at_thread_exit(9); }).detach();

  std::cout << "Waiting..." << std::flush;
  f1.wait();
  f2.wait();
  f3.wait();
  std::cout << "Done!\nResults are: " << f1.get() << ' ' << f2.get() << ' '
            << f3.get() << '\n';
  t.join();
}
} // namespace future_test

//
// https://en.cppreference.com/w/cpp/thread/future/valid#Example
//
#include <future>
#include <iostream>

namespace future_valid_test {
void run() {
  std::promise<void> p;
  std::future<void> f = p.get_future();

  std::cout << std::boolalpha;

  std::cout << f.valid() << '\n';
  assert(f.valid());
  p.set_value();
  std::cout << f.valid() << '\n';
  assert(f.valid());
  f.get();
  std::cout << f.valid() << '\n';
  assert(!f.valid());
}
} // namespace future_valid_test

//
// https://en.cppreference.com/w/cpp/thread/future#Example_with_exceptions
//
#include <future>
#include <iostream>
#include <stdexcept>
#include <thread>

namespace future_exception_test {
void run() {
  std::promise<int> p;
  std::future<int> f = p.get_future();
  std::thread t([&p] {
    try {
      // code that may throw
      throw std::runtime_error("Example");
    } catch (...) {
      try {
        // store anything thrown in the promise
        p.set_exception(std::current_exception());
      } catch (...) {
      } // set_exception() may throw too
    }
  });
  try {
    std::cout << f.get();
    assert(false);
  } catch (const std::exception &e) {
    std::cout << "Exception from the thread: " << e.what() << '\n';
  }
  t.join();
}
} // namespace future_exception_test

//
// https://en.cppreference.com/w/cpp/thread/promise/set_exception#Example
//
#include <future>
#include <iostream>
#include <stdexcept>
#include <thread>

namespace promise_set_exception_test {
void run() {
  std::promise<int> p;
  std::future<int> f = p.get_future();
  std::thread t([&p] {
    try {
      // code that may throw
      throw std::runtime_error("Example");
    } catch (...) {
      try {
        // store anything thrown in the promise
        p.set_exception(std::current_exception());
        // or throw a custom exception instead
        // p.set_exception(std::make_exception_ptr(MyException("mine")));
      } catch (...) {
      } // set_exception() may throw too
    }
  });
  try {
    std::cout << f.get();
    assert(false);
  } catch (const std::exception &e) {
    std::cout << "Exception from the thread: " << e.what() << '\n';
  }
  t.join();
}
} // namespace promise_set_exception_test

//
// https://en.cppreference.com/w/cpp/thread/async#Example
//
#include <algorithm>
#include <future>
#include <iostream>
#include <mutex>
#include <numeric>
#include <string>
#include <vector>

namespace async_test {
std::mutex m;

struct X {
  void foo(int i, const std::string &str) {
    std::lock_guard<std::mutex> lk(m);
    std::cout << str << ' ' << i << '\n';
  }

  void bar(const std::string &str) {
    std::lock_guard<std::mutex> lk(m);
    std::cout << str << '\n';
  }

  int operator()(int i) {
    std::lock_guard<std::mutex> lk(m);
    std::cout << i << '\n';
    return i + 10;
  }
};

template <typename RandomIt> int parallel_sum(RandomIt beg, RandomIt end) {
  auto len = end - beg;
  if (len < 1000) {
    return std::accumulate(beg, end, 0);
  }
  RandomIt mid = beg + len / 2;
  auto handle =
      std::async(std::launch::async, parallel_sum<RandomIt>, mid, end);
  int sum = parallel_sum(beg, mid);
  return sum + handle.get();
}

void run() {
  std::vector<int> v(10000, 1);
  const int sum = parallel_sum(v.begin(), v.end());
  std::cout << "The sum is " << sum << '\n';
  assert(sum == 10000);

  X x;
  // Calls (&x)->foo(42, "Hello") with default policy:
  // may print "Hello 42" concurrently or defer execution
  auto a1 = std::async(&X::foo, &x, 42, "Hello");
  // Calls x.bar("world!") with deferred policy
  // prints "world!" when a2.get() or a2.wait() is called
  auto a2 = std::async(std::launch::deferred, &X::bar, x, "world!");
  // Calls X()(43); with async policy
  // prints "43" concurrently
  auto a3 = std::async(std::launch::async, X(), 43);
  a2.wait(); // prints "world!"
  const int result = a3.get();
  std::cout << result << '\n'; // prints "53"
  assert(result == 53);
  a1.wait();
}
} // namespace async_test

//
// https://en.cppreference.com/w/cpp/thread/future/wait_for#Example
//
#include <chrono>
#include <future>
#include <iostream>
#include <thread>

namespace future_wait_for_test {
using namespace std::chrono_literals;

void run() {
  std::future<int> future = std::async(std::launch::async, []() {
    std::this_thread::sleep_for(3s);
    return 8;
  });

  std::cout << "waiting...\n";
  std::future_status status;
  do {
    switch (status = future.wait_for(1s); status) {
    case std::future_status::deferred:
      std::cout << "deferred\n";
      break;
    case std::future_status::timeout:
      std::cout << "timeout\n";
      break;
    case std::future_status::ready:
      std::cout << "ready!\n";
      break;
    }
  } while (status != std::future_status::ready);
  std::cout << "result is " << future.get() << '\n';
}
} // namespace future_wait_for_test

//
// https://en.cppreference.com/w/cpp/thread/future/wait_until#Example
//
#include <chrono>
#include <future>
#include <iostream>
#include <thread>

namespace future_status_test {
void run() {
  std::chrono::system_clock::time_point two_seconds_passed =
      std::chrono::system_clock::now() + std::chrono::seconds(2);
  // Make a future that takes 1 second to complete
  std::promise<int> p1;
  std::future<int> f_completes = p1.get_future();
  std::thread([](std::promise<int> p1) {
    std::this_thread::sleep_for(std::chrono::seconds(1));
    p1.set_value_at_thread_exit(9);
  }, std::move(p1))
      .detach();
  // Make a future that takes 5 seconds to complete
  std::promise<int> p2;
  std::future<int> f_times_out = p2.get_future();
  std::thread([](std::promise<int> p2) {
    std::this_thread::sleep_for(std::chrono::seconds(5));
    p2.set_value_at_thread_exit(8);
  }, std::move(p2))
      .detach();

  std::cout << "Waiting for 2 seconds..." << std::endl;
  if (std::future_status::ready == f_completes.wait_until(two_seconds_passed))
    std::cout << "f_completes: " << f_completes.get() << "\n";
  else
    std::cout << "f_completes did not complete!\n";

  if (std::future_status::ready == f_times_out.wait_until(two_seconds_passed))
    std::cout << "f_times_out: " << f_times_out.get() << "\n";
  else
    std::cout << "f_times_out did not complete!\n";

  std::cout << "Done!\n";
}
} // namespace future_status_test

//
// https://en.cppreference.com/w/cpp/thread/future_error#Example
//
#include <future>
#include <iostream>

namespace future_error_test {
void run() {
  std::promise<int> promise;
  (void)promise.get_future();

  try {
    (void)promise.get_future();
    assert(false);
  } catch (const std::future_error &e) {
    // cppreference demonstrates std::future_error with an empty future get(),
    // but that path is undefined behavior. Use the specified
    // future_already_retrieved error path in the driver harness.
    std::cout << "Caught a future_error with code \"" << e.code()
              << "\"\n Message: \"" << e.what() << "\"\n";
    assert(e.code() ==
           std::make_error_code(std::future_errc::future_already_retrieved));
  }
}
} // namespace future_error_test

//
// https://en.cppreference.com/w/cpp/thread/shared_future#Example
//
#include <chrono>
#include <future>
#include <iostream>

namespace shared_future_test {
void run() {
  std::promise<void> ready_promise, t1_ready_promise, t2_ready_promise;
  std::shared_future<void> ready_future(ready_promise.get_future());

  std::chrono::time_point<std::chrono::high_resolution_clock> start;
  auto fun1 = [&, ready_future]() -> std::chrono::duration<double, std::milli> {
    t1_ready_promise.set_value();
    ready_future.wait(); // waits for the signal from main()
    return std::chrono::high_resolution_clock::now() - start;
  };
  auto fun2 = [&, ready_future]() -> std::chrono::duration<double, std::milli> {
    t2_ready_promise.set_value();
    ready_future.wait(); // waits for the signal from main()
    return std::chrono::high_resolution_clock::now() - start;
  };

  auto fut1 = t1_ready_promise.get_future();
  auto fut2 = t2_ready_promise.get_future();
  auto result1 = std::async(std::launch::async, fun1);
  auto result2 = std::async(std::launch::async, fun2);

  // wait for the threads to become ready
  fut1.wait();
  fut2.wait();

  // the threads are ready, start the clock
  start = std::chrono::high_resolution_clock::now();

  // signal the threads to go
  ready_promise.set_value();
  auto elapsed1 = result1.get();
  auto elapsed2 = result2.get();
  std::cout << "Thread 1 received the signal " << elapsed1.count()
            << " ms after start\n"
            << "Thread 2 received the signal " << elapsed2.count()
            << " ms after start\n";

  assert(elapsed1.count() >= 0);
  assert(elapsed2.count() >= 0);
}
} // namespace shared_future_test

//
// https://en.cppreference.com/w/cpp/thread/shared_future/wait_for#Example
//
#include <chrono>
#include <future>
#include <iostream>
#include <thread>

namespace shared_future_wait_for_test {
using namespace std::chrono_literals;

void run() {
  std::shared_future<int> future = std::async(std::launch::async, []() {
    std::this_thread::sleep_for(3s);
    return 8;
  });

  std::cout << "waiting...\n";
  std::future_status status;
  do {
    switch (status = future.wait_for(1s); status) {
    case std::future_status::deferred:
      std::cout << "deferred\n";
      break;
    case std::future_status::timeout:
      std::cout << "timeout\n";
      break;
    case std::future_status::ready:
      std::cout << "ready!\n";
      break;
    }
  } while (status != std::future_status::ready);
  std::cout << "result is " << future.get() << '\n';
}
} // namespace shared_future_wait_for_test

//
// https://en.cppreference.com/w/cpp/thread/future/wait_for
// https://en.cppreference.com/w/cpp/thread/shared_future/wait_for
//
namespace future_timeout_edge_test {
void run() {
  std::promise<int> promise;
  std::future<int> future = promise.get_future();
  assert(future.wait_for(std::chrono::milliseconds(1)) ==
         std::future_status::timeout);
  promise.set_value(7);
  assert(future.wait_for(std::chrono::milliseconds(1)) ==
         std::future_status::ready);
  assert(future.get() == 7);

  std::promise<int> shared_promise;
  std::shared_future<int> shared_future =
      shared_promise.get_future().share();
  assert(shared_future.wait_for(std::chrono::milliseconds(1)) ==
         std::future_status::timeout);
  shared_promise.set_value(9);
  assert(shared_future.wait_for(std::chrono::milliseconds(1)) ==
         std::future_status::ready);
  assert(shared_future.get() == 9);
}
} // namespace future_timeout_edge_test

//
// https://en.cppreference.com/w/cpp/thread/promise#Example
//
#include <chrono>
#include <future>
#include <iostream>
#include <numeric>
#include <thread>
#include <vector>

namespace promise_test {
void accumulate(std::vector<int>::iterator first,
                std::vector<int>::iterator last,
                std::promise<int> accumulate_promise) {
  int sum = std::accumulate(first, last, 0);
  accumulate_promise.set_value(sum); // Notify future
}

void do_work(std::promise<void> barrier) {
  std::this_thread::sleep_for(std::chrono::seconds(1));
  barrier.set_value();
}

void run() {
  // Demonstrate using promise<int> to transmit a result between threads.
  std::vector<int> numbers = {1, 2, 3, 4, 5, 6};
  std::promise<int> accumulate_promise;
  std::future<int> accumulate_future = accumulate_promise.get_future();
  std::thread work_thread(accumulate, numbers.begin(), numbers.end(),
                          std::move(accumulate_promise));

  // future::get() will wait until the future has a valid result and retrieves
  // it. Calling wait() before get() is not needed
  // accumulate_future.wait();  // wait for result
  std::cout << "result=" << accumulate_future.get() << '\n';
  work_thread.join(); // wait for thread completion

  // Demonstrate using promise<void> to signal state between threads.
  std::promise<void> barrier;
  std::future<void> barrier_future = barrier.get_future();
  std::thread new_work_thread(do_work, std::move(barrier));
  barrier_future.wait();
  new_work_thread.join();
}
} // namespace promise_test

//
// https://en.cppreference.com/w/cpp/thread/packaged_task#Example
//
#include <cmath>
#include <functional>
#include <future>
#include <iostream>
#include <thread>

namespace packaged_task_test {
// unique function to avoid disambiguating the std::pow overload set
int f(int x, int y) { return (int)std::pow(x, y); }

void task_lambda() {
  std::packaged_task<int(int, int)> task(
      [](int a, int b) { return (int)std::pow(a, b); });
  std::future<int> result = task.get_future();

  task(2, 9);

  std::cout << "task_lambda:\t" << result.get() << '\n';
}

void task_bind() {
  std::packaged_task<int()> task(std::bind(f, 2, 11));
  std::future<int> result = task.get_future();

  task();

  std::cout << "task_bind:\t" << result.get() << '\n';
}

void task_thread() {
  std::packaged_task<int(int, int)> task(f);
  std::future<int> result = task.get_future();

  std::thread task_td(std::move(task), 2, 10);
  task_td.join();

  std::cout << "task_thread:\t" << result.get() << '\n';
}

void run() {
  task_lambda();
  task_bind();
  task_thread();
}
} // namespace packaged_task_test

//
// https://en.cppreference.com/w/cpp/thread/latch#Example
//
#include <array>
#include <functional>
#include <latch>
#include <string>
#include <thread>

namespace latch_test {
struct Job {
  const std::string name;
  std::string product{"not worked"};
  std::thread action{};
};

void run() {
  std::array<Job, 3> jobs{{{"Annika"}, {"Buru"}, {"Chuck"}}};

  std::latch work_done{static_cast<std::ptrdiff_t>(jobs.size())};
  std::latch start_clean_up{1};
  auto work = [&](Job &my_job) {
    my_job.product = my_job.name + " worked";
    work_done.count_down();
    start_clean_up.wait();
    my_job.product = my_job.name + " cleaned";
  };

  std::cout << "Work is starting... ";
  for (auto &job : jobs) {
    job.action = std::thread{work, std::ref(job)};
  }
  work_done.wait();
  std::cout << "done:\n";
  for (const auto &job : jobs) {
    std::cout << "  " << job.product << '\n';
  }

  std::cout << "Workers are cleaning up... ";
  start_clean_up.count_down();
  for (auto &job : jobs) {
    job.action.join();
  }

  std::cout << "done:\n";
  for (const auto &job : jobs) {
    std::cout << "  " << job.product << '\n';
  }
}
} // namespace latch_test

//
// https://en.cppreference.com/w/cpp/thread/barrier#Example
//
#include <array>
#include <barrier>
#include <iostream>
#include <string>
#include <syncstream>
#include <thread>
#include <vector>

namespace barrier_test {
void run() {
  const auto workers = {"Anil", "Busara", "Carl"};
  auto on_completion = []() noexcept {
    // locking not needed here
    static auto phase = "... done\n"
                        "Cleaning up...\n";
    std::cout << phase;
    phase = "... done\n";
  };

  std::barrier sync_point(std::ssize(workers), on_completion);
  auto work = [&](std::string name) {
    std::string product = "  " + name + " worked\n";
    std::osyncstream(std::cout) << product; // ok, op<< call is atomic
    sync_point.arrive_and_wait();

    product = "  " + name + " cleaned\n";
    std::osyncstream(std::cout) << product;
    sync_point.arrive_and_wait();
  };
  std::cout << "Starting...\n";
  std::vector<std::jthread> threads;
  threads.reserve(std::size(workers));
  for (auto const &worker : workers) {
    threads.emplace_back(work, worker);
  }
}
} // namespace barrier_test

//
// https://en.cppreference.com/w/cpp/thread/counting_semaphore#Example
//
#include <atomic>
#include <chrono>
#include <cstdio>
#include <iostream>
#include <semaphore>
#include <stop_token>
#include <thread>

namespace counting_semaphore_test {
// global binary semaphore instances
// object counts are set to zero
// objects are in non-signaled state
std::binary_semaphore smphSignalMainToThread{0}, smphSignalThreadToMain{0};

void ThreadProc() {
  // wait for a signal from the main proc
  // by attempting to decrement the semaphore
  smphSignalMainToThread.acquire();
  // this call blocks until the semaphore's count
  // is increased from the main proc

  std::cout << "[thread] Got the signal\n";

  // wait for 3 seconds to imitate some work
  // being done by the thread
  using namespace std::literals;
  std::this_thread::sleep_for(3s);

  std::cout << "[thread] Send the signal\n";

  // signal the main proc back
  smphSignalThreadToMain.release();
}

void run() {
  // create some worker thread
  std::thread thrWorker(ThreadProc);

  std::cout << "[main] Send the signal\n";

  // signal the worker thread to start working
  // by increasing the semaphore's count
  smphSignalMainToThread.release();

  // wait until the worker thread is done doing the work
  // by attempting to decrement the semaphore's count
  smphSignalThreadToMain.acquire();
  std::cout << "[main] Got the signal\n";
  thrWorker.join();
}
} // namespace counting_semaphore_test

//
// https://en.cppreference.com/w/cpp/thread/jthread/jthread#Example
//
#include <chrono>
#include <iostream>
#include <thread>
#include <utility>

namespace jthread_constructor_test {
using namespace std::literals;

void f1(int n) {
  for (int i = 0; i < 5; ++i) {
    std::cout << "Thread 1 executing\n";
    ++n;
    std::this_thread::sleep_for(10ms);
  }
}

void f2(int &n) {
  for (int i = 0; i < 5; ++i) {
    std::cout << "Thread 2 executing\n";
    ++n;
    std::this_thread::sleep_for(10ms);
  }
}

class foo {
public:
  void bar() {
    for (int i = 0; i < 5; ++i) {
      std::cout << "Thread 3 executing\n";
      ++n;
      std::this_thread::sleep_for(10ms);
    }
  }
  int n = 0;
};

class baz {
public:
  void operator()() {
    for (int i = 0; i < 5; ++i) {
      std::cout << "Thread 4 executing\n";
      ++n;
      std::this_thread::sleep_for(10ms);
    }
  }
  int n = 0;
};

void run() {
  int n = 0;
  foo f;
  baz b;
  std::jthread t0;                 // t0 is not a thread
  std::jthread t1(f1, n + 1);      // pass by value
  std::jthread t2a(f2, std::ref(n)); // pass by reference
  std::jthread t2b(std::move(t2a)); // t2b is now running f2(). t2a is no longer a thread
  std::jthread t3(&foo::bar, &f);  // t3 runs foo::bar() on object f
  std::jthread t4(b);              // t4 runs baz::operator() on a copy of object b
  t1.join();
  t2b.join();
  t3.join();
  std::cout << "Final value of n is " << n << '\n';
  std::cout << "Final value of f.n (foo::n) is " << f.n << '\n';
  std::cout << "Final value of b.n (baz::n) is " << b.n << '\n';
  // t4 joins on destruction
}
} // namespace jthread_constructor_test

//
// https://en.cppreference.com/w/cpp/thread/stop_source#Example
//
#include <chrono>
#include <cstdio>
#include <iostream>
#include <stop_token>
#include <thread>

namespace stop_source_test {
using namespace std::chrono_literals;

void worker_fun(int id, std::stop_token stoken) {
  for (int i = 10; i; --i) {
    std::this_thread::sleep_for(300ms);
    if (stoken.stop_requested()) {
      std::printf("  worker%d is requested to stop\n", id);
      return;
    }
    std::printf("  worker%d goes back to sleep\n", id);
  }
}

void run() {
  std::jthread threads[4];
  std::cout << std::boolalpha;
  auto print = [](const std::stop_source &source) {
    std::printf("stop_source stop_possible = %s, stop_requested = %s\n",
                source.stop_possible() ? "true" : "false",
                source.stop_requested() ? "true" : "false");
  };

  // Common source
  std::stop_source stop_source;

  print(stop_source);
  // Create worker threads
  for (int i = 0; i < 4; ++i) {
    threads[i] = std::jthread(worker_fun, i + 1, stop_source.get_token());
  }

  std::this_thread::sleep_for(500ms);

  std::puts("Request stop");
  stop_source.request_stop();

  print(stop_source);

  // Note: destructor of jthreads will call join so no need for explicit calls
}
} // namespace stop_source_test

//
// https://en.cppreference.com/w/cpp/thread/stop_callback#Example
//
#include <chrono>
#include <condition_variable>
#include <iostream>
#include <mutex>
#include <sstream>
#include <thread>

namespace stop_callback_test {
using namespace std::chrono_literals;

// Use a helper class for atomic std::cout streaming.
class Writer {
  std::ostringstream buffer;

public:
  ~Writer() { std::cout << buffer.str(); }

  Writer &operator<<(auto input) {
    buffer << input;
    return *this;
  }
};

void run() {
  // A worker thread.
  // It will wait until it is requested to stop.
  std::jthread worker([](std::stop_token stoken) {
    Writer() << "Worker thread's id: " << std::this_thread::get_id() << '\n';
    std::mutex mutex;
    std::unique_lock lock(mutex);
    std::condition_variable_any().wait(
        lock, stoken, [&stoken] { return stoken.stop_requested(); });
  });

  // Register a stop callback on the worker thread.
  std::stop_callback callback(worker.get_stop_token(), [] {
    Writer() << "Stop callback executed by thread: "
             << std::this_thread::get_id() << '\n';
  });

  // Stop_callback objects can be destroyed prematurely to prevent execution.
  {
    std::stop_callback scoped_callback(worker.get_stop_token(), [] {
      // This will not be executed.
      Writer() << "Scoped stop callback executed by thread: "
               << std::this_thread::get_id() << '\n';
    });
  }

  // Demonstrate which thread executes the stop_callback and when.
  // Define a stopper function.
  auto stopper_func = [&worker] {
    if (worker.request_stop()) {
      Writer() << "Stop request executed by thread: "
               << std::this_thread::get_id() << '\n';
    } else {
      Writer() << "Stop request not executed by thread: "
               << std::this_thread::get_id() << '\n';
    }
  };

  // Let multiple threads compete for stopping the worker thread.
  std::jthread stopper1(stopper_func);
  std::jthread stopper2(stopper_func);
  stopper1.join();
  stopper2.join();

  // After a stop has already been requested,
  // a new stop_callback executes immediately.
  Writer() << "Main thread: " << std::this_thread::get_id() << '\n';
  std::stop_callback callback_after_stop(worker.get_stop_token(), [] {
    Writer() << "Stop callback executed by thread: "
             << std::this_thread::get_id() << '\n';
  });
}
} // namespace stop_callback_test
