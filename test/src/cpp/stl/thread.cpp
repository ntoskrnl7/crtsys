#include "../../test.h"

//
// https://en.cppreference.com/w/cpp/thread/condition_variable#Example
//
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
  // std::cout << "Worker thread is processing data\n";
  printf("Worker thread is processing data\n");
  data += " after processing";

  // Send data back to main()
  processed = true;
  // std::cout << "Worker thread signals data processing completed\n";
  printf("Worker thread signals data processing completed\n");

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
    // std::cout << "main() signals data ready for processing\n";
    printf("condition_variable_test() signals data ready for processing\n");
  }
  cv.notify_one();

  // wait for the worker
  {
    std::unique_lock lk(m);
    cv.wait(lk, [] { return processed; });
  }
  // std::cout << "Back in main(), data = " << data << '\n';
  printf("Back in condition_variable_test(), data = %s\n", data.c_str());

  worker.join();
}
} // namespace condition_variable_test

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
    // std::cout << pair.first << " => " << pair.second << '\n';
    printf("%s => %s\n", pair.first.c_str(), pair.second.c_str());
  }
}
} // namespace mutex_test

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
      // std::cout << std::this_thread::get_id() << ' ' << counter.increment()
      // << '\n';
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
} // namespace shared_mutex_test

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

  // std::cout << "Waiting..." << std::flush;
  printf("Waiting...\n");
  f1.wait();
  f2.wait();
  f3.wait();
  // std::cout << "Done!\nResults are: " << f1.get() << ' ' << f2.get() << ' '
  // << f3.get() << '\n';
  printf("Done!\nResults are: %d, %d, %d\n", f1.get(), f2.get(), f3.get());
  t.join();
}
} // namespace future_test

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

void run() {
  task_lambda();
  task_bind();
  task_thread();
}
} // namespace packaged_task_test