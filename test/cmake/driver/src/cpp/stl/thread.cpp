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
    std::cout
        << "condition_variable_test::run() signals data ready for processing\n";
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
