#include <wdm.h>

#if !DBG
#pragma warning(disable:4702)
#endif

#include <stdio.h>
#define printf(...)                                                            \
  (DbgPrintEx(DPFLTR_IHVDRIVER_ID, DPFLTR_ERROR_LEVEL, __VA_ARGS__))

//
// https://en.cppreference.com/w/cpp/language/throw
//
#include <iostream>
#include <stdexcept>

struct A {
  int n;

  A(int n = 0) : n(n) { printf("A(%d) constructed successfully\n", n); }
  ~A() { printf("A(%d) destroyed\n", n); }
};

int foo() { throw std::runtime_error("error"); }

struct B {
  A a1, a2, a3;

  B() try : a1(1), a2(foo()), a3(3) {
    printf("B constructed successfully\n");
  } catch (...) {
    printf("B::B() exiting with exception\n");
  }

  ~B() { printf("B destroyed\n"); }
};

struct C : A, B {
  C() try { printf("C::C() completed successfully\n"); } catch (...) {
    printf("C::C() exiting with exception\n");
  }

  ~C() { printf("C destroyed\n"); }
};

void throw_test() try {
  // creates the A base subobject
  // creates the a1 member of B
  // fails to create the a2 member of B
  // unwinding destroys the a1 member of B
  // unwinding destroys the A base subobject
  C c;
} catch (const std::exception &e) {
  printf("main() failed to create C with: %s", e.what());
}

//
// https://en.cppreference.com/w/cpp/thread/condition_variable
//
#include <condition_variable>
#include <iostream>
#include <mutex>
#include <string>
#include <thread>

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

  printf("Worker thread is processing data\n");
  data += " after processing";

  // Send data back to main()
  processed = true;
  printf("Worker thread signals data processing completed\n");

  // Manual unlocking is done before notifying, to avoid waking up
  // the waiting thread only to block again (see notify_one for details)
  lk.unlock();
  cv.notify_one();
}

void condition_variable_test() {
  std::thread worker(worker_thread);

  data = "Example data";
  // send data to the worker thread
  {
    std::lock_guard lk(m);
    ready = true;
    printf("condition_variable_test() signals data ready for processing\n");
  }
  cv.notify_one();

  // wait for the worker
  {
    std::unique_lock lk(m);
    cv.wait(lk, [] { return processed; });
  }
  printf("Back in condition_variable_test(), data = %s\n", data.c_str());
  worker.join();
}

//
// https://en.cppreference.com/w/cpp/thread/mutex
//
#include <chrono>
#include <iostream>
#include <map>
#include <mutex>
#include <string>
#include <thread>

std::map<std::string, std::string> g_pages;
std::mutex g_pages_mutex;

void save_page(const std::string &url) {
  // simulate a long page fetch
  std::this_thread::sleep_for(std::chrono::seconds(2));
  std::string result = "fake content";

  std::lock_guard<std::mutex> guard(g_pages_mutex);
  g_pages[url] = result;
}

void mutex_test() {
  std::thread t1(save_page, "http://foo");
  std::thread t2(save_page, "http://bar");
  t1.join();
  t2.join();

  // safe to access g_pages without lock now, as the threads are joined
  for (const auto &pair : g_pages) {
    printf("%s => %s\n", pair.first.c_str(), pair.second.c_str());
  }
}

//
// https://en.cppreference.com/w/cpp/thread/shared_mutex
//
#include <iostream>
#include <mutex>
#include <shared_mutex>
#include <thread>

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

void shared_mutex_test() {
  ThreadSafeCounter counter;

  auto increment_and_print = [&counter]() {
    for (int i = 0; i < 3; i++) {
      printf("%p %d\n", std::this_thread::get_id(), counter.increment());

      // Note: Writing to std::cout actually needs to be synchronized as well
      // by another std::mutex. This has been omitted to keep the example small.
    }
  };

  std::thread thread1(increment_and_print);
  std::thread thread2(increment_and_print);

  thread1.join();
  thread2.join();
}

//
// https://en.cppreference.com/w/cpp/thread/future
//
#include <future>
#include <iostream>
#include <thread>

void future_test() {
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

  printf("Waiting...\n");

  f1.wait();
  f2.wait();
  f3.wait();
  printf("Done!\nResults are: %d, %d, %d\n", f1.get(), f2.get(), f3.get());

  t.join();
}