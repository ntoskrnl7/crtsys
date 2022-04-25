#include <wdm.h>

#if !DBG
#pragma warning(disable : 4702)
#endif

#include <stdio.h>
#define printf(...)                                                            \
  (DbgPrintEx(DPFLTR_IHVDRIVER_ID, DPFLTR_ERROR_LEVEL, __VA_ARGS__))

//
// https://en.cppreference.com/w/cpp/language/throw#Example
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
  printf("main() failed to create C with: %s\n", e.what());
}

//
// https://en.cppreference.com/w/cpp/chrono#Example
//
#include <chrono>
#include <iostream>

long fibonacci(unsigned n) {
  if (n < 2)
    return n;
  return fibonacci(n - 1) + fibonacci(n - 2);
}

void chrono_test() {
  auto start = std::chrono::steady_clock::now();
  // std::cout << "f(42) = " << fibonacci(42) << '\n';
  printf("f(40) = %d\n", fibonacci(40));
  auto end = std::chrono::steady_clock::now();
  // std::chrono::duration<double> elapsed_seconds = end - start;
  // std::cout << "elapsed time: " << elapsed_seconds.count() << "s\n";
  printf("elapsed time: %dms\n",
         std::chrono::duration_cast<std::chrono::milliseconds>(end - start)
             .count());
}

//
// https://en.cppreference.com/w/cpp/thread/condition_variable#Example
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
// https://en.cppreference.com/w/cpp/thread/mutex#Example
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
// https://en.cppreference.com/w/cpp/thread/shared_mutex#Example
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
// https://en.cppreference.com/w/cpp/thread/future#Example
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

//
// https://en.cppreference.com/w/cpp/thread/promise#Example
//
#include <chrono>
#include <future>
#include <iostream>
#include <numeric>
#include <thread>
#include <vector>

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

void promise_test() {
  // Demonstrate using promise<int> to transmit a result between threads.
  std::vector<int> numbers = {1, 2, 3, 4, 5, 6};
  std::promise<int> accumulate_promise;
  std::future<int> accumulate_future = accumulate_promise.get_future();
  std::thread work_thread(accumulate, numbers.begin(), numbers.end(),
                          std::move(accumulate_promise));

  // future::get() will wait until the future has a valid result and retrieves
  // it. Calling wait() before get() is not needed
  // accumulate_future.wait();  // wait for result
  // std::cout << "result=" << accumulate_future.get() << '\n';
  printf("result=%d\n", accumulate_future.get());
  work_thread.join(); // wait for thread completion

  // Demonstrate using promise<void> to signal state between threads.
  std::promise<void> barrier;
  std::future<void> barrier_future = barrier.get_future();
  std::thread new_work_thread(do_work, std::move(barrier));
  barrier_future.wait();
  new_work_thread.join();
}

//
// https://en.cppreference.com/w/cpp/thread/packaged_task#Example
//
#include <cmath>
#include <functional>
#include <future>
#include <iostream>
#include <thread>

// unique function to avoid disambiguating the std::pow overload set
int f(int x, int y) { return (int)std::pow(x, y); }

void task_lambda() {
  std::packaged_task<int(int, int)> task(
      [](int a, int b) { return (int)std::pow(a, b); });
  std::future<int> result = task.get_future();

  task(2, 9);

  // std::cout << "task_lambda:\t" << result.get() << '\n';
  printf("task_lambda:\t%d\n", result.get());
}

void task_bind() {
  std::packaged_task<int()> task(std::bind(f, 2, 11));
  std::future<int> result = task.get_future();

  task();

  // std::cout << "task_bind:\t" << result.get() << '\n';
  printf("task_bind:\t%d\n", result.get());
}

void task_thread() {
  std::packaged_task<int(int, int)> task(f);
  std::future<int> result = task.get_future();

  std::thread task_td(std::move(task), 2, 10);
  task_td.join();

  // std::cout << "task_thread:\t" << result.get() << '\n';
  printf("task_thread:\t%d\n", result.get());
}

void packaged_task() {
  task_lambda();
  task_bind();
  task_thread();
}

//
// https://en.cppreference.com/w/cpp/language/try_catch#Example
//
#include <iostream>
#include <vector>

void try_catch_test() {
  try {
    printf("Throwing an integer exception...\n");
    // std::cout << "Throwing an integer exception...\n";
    throw 42;
  } catch (int i) {
    printf(" the integer exception was caught, with value: %d\n", i);
    // std::cout << " the integer exception was caught, with value: " << i
    // << '\n';
  }

  try {
    printf("Creating a vector of size 5... \n");
    // std::cout << "Creating a vector of size 5... \n";
    std::vector<int> v(5);
    printf("Accessing the 11th element of the vector...\n");
    // std::cout << "Accessing the 11th element of the vector...\n";
    printf("%d", v.at(10));
    // std::cout << v.at(10);          // vector::at() throws std::out_of_range
  } catch (const std::exception &e) // caught by reference to base
  {
    printf("a standard exception was caught, with message '%s'\n", e.what());
    // std::cout << " a standard exception was caught, with message '" <<
    // e.what()
    //           << "'\n";
  }
}