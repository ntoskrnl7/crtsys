# Feature Coverage

[Back to README](../README.md)

This page keeps the detailed support checklist that used to live in the main
README. Checked items are implemented and have test coverage when a `tested`
link is attached.

## C++ Standard

The tests are based on examples and behavior described by
[cppreference](https://en.cppreference.com).

### Initialization

- [x] [Non-local variables](https://en.cppreference.com/w/cpp/language/initialization#Non-local_variables)
  - [x] [Static initialization](https://en.cppreference.com/w/cpp/language/initialization#Static_initialization)
    - [x] [Constant initialization](https://en.cppreference.com/w/cpp/language/constant_initialization)
      [(tested)](../test/driver/src/cpp/lang/initialization.cpp#L13)
    - [x] [Zero initialization](https://en.cppreference.com/w/cpp/language/zero_initialization)
      [(tested)](../test/driver/src/cpp/lang/initialization.cpp#L41)
  - [x] [Dynamic initialization](https://en.cppreference.com/w/cpp/language/initialization#Dynamic_initialization)
    [(tested)](../test/driver/src/cpp/lang/initialization.cpp#L65)
- [ ] [Static local variables](https://en.cppreference.com/w/cpp/language/storage_duration#Static_local_variables)
  - [ ] `thread_local`
  - [ ] function-local `static`

### Exceptions

- [x] [throw](https://en.cppreference.com/w/cpp/language/throw)
  [(tested)](../test/driver/src/cpp/lang/exceptions.cpp#L42)
- [x] [try block](https://en.cppreference.com/w/cpp/language/try_catch)
  [(tested)](../test/driver/src/cpp/lang/exceptions.cpp#L60)
- [x] [Function try block](https://en.cppreference.com/w/cpp/language/function-try-block)
  [(tested)](../test/driver/src/cpp/lang/exceptions.cpp#L98)

## Microsoft STL

- [x] [std::chrono](https://en.cppreference.com/w/cpp/chrono)
  [(tested)](../test/driver/src/cpp/stl/chrono.cpp#L15)
- [x] [std::thread](https://en.cppreference.com/w/cpp/thread)
  [(tested)](../test/driver/src/cpp/stl/thread.cpp#L35)
- [x] [std::condition_variable](https://en.cppreference.com/w/cpp/thread/condition_variable)
  [(tested)](../test/driver/src/cpp/stl/thread.cpp#L35)
- [x] [std::mutex](https://en.cppreference.com/w/cpp/thread/mutex)
  [(tested)](../test/driver/src/cpp/stl/thread.cpp#L81)
- [x] [std::shared_mutex](https://en.cppreference.com/w/cpp/thread/shared_mutex)
  [(tested)](../test/driver/src/cpp/stl/thread.cpp#L129)
- [x] [std::future](https://en.cppreference.com/w/cpp/thread/future)
  [(tested)](../test/driver/src/cpp/stl/thread.cpp#L157)
- [x] [std::promise](https://en.cppreference.com/w/cpp/thread/promise)
  [(tested)](../test/driver/src/cpp/stl/thread.cpp#L203)
- [x] [std::packaged_task](https://en.cppreference.com/w/cpp/thread/packaged_task)
  [(tested)](../test/driver/src/cpp/stl/thread.cpp#L267)
- [x] [std::cin](https://en.cppreference.com/w/cpp/io/cin)
- [x] [std::cout](https://en.cppreference.com/w/cpp/io/cout)
- [x] [std::cerr](https://en.cppreference.com/w/cpp/io/cerr)
- [x] [std::clog](https://en.cppreference.com/w/cpp/io/clog)
- [x] [std::wcin](https://en.cppreference.com/w/cpp/io/cin)
- [x] [std::wcout](https://en.cppreference.com/w/cpp/io/cout)
- [x] [std::wcerr](https://en.cppreference.com/w/cpp/io/cerr)
- [x] [std::wclog](https://en.cppreference.com/w/cpp/io/clog)
- [x] `nlohmann::json` integration
  [(tested)](../test/driver/src/libs/nlohmann_json.cpp)

## C Standard

- [x] Math functions
  - adapted where needed from small portable implementations
  - references:
    [RetrievAL](https://github.com/SpoilerScriptsGroup/RetrievAL),
    [musl](https://github.com/bminor/musl)
- [x] Floating-point classification helpers
  [(source)](../src/custom/crt/math/fpclassify.c)

## NTL: NT Template Library

NTL provides C++ helpers for driver code. See the
[NTL API reference](./ntl-api.md) for API-level details.

- [x] `ntl::expand_stack`
  [(tested)](../test/driver/src/ntl.cpp#L5)
- [x] `ntl::status`
- [x] `ntl::driver`
  - [x] unload callback
    [(tested)](../test/driver/src/main.cpp#L73)
  - [x] device creation
    [(tested)](../test/driver/src/main.cpp#L44)
- [x] `ntl::device`
  - [x] device extension
    [(tested)](../test/driver/src/main.cpp#L33)
  - [ ] `IRP_MJ_CREATE`
  - [ ] `IRP_MJ_CLOSE`
  - [x] `IRP_MJ_DEVICE_CONTROL`
    [(app test)](../test/app/src/main.cpp#L77)
    [(driver test)](../test/driver/src/main.cpp#L55)
- [x] `ntl::main`
  [(tested)](../test/driver/src/main.cpp#L25)
- [x] `ntl::rpc`
  - [x] `ntl::rpc::server`
    [(tested)](../test/driver/src/main.cpp#L19)
    [(schema)](../test/common/rpc.hpp)
  - [x] `ntl::rpc::client`
    [(tested)](../test/app/src/main.cpp#L4)
    [(schema)](../test/common/rpc.hpp)
- [x] `ntl::irql`
  - [x] `ntl::irql`
    [(tested)](../test/driver/src/ntl.cpp#L46)
  - [x] `ntl::raise_irql`
    [(tested)](../test/driver/src/ntl.cpp#L49)
  - [x] `ntl::raise_irql_to_dpc_level`
    [(tested)](../test/driver/src/ntl.cpp#L62)
  - [x] `ntl::raise_irql_to_synch_level`
    [(tested)](../test/driver/src/ntl.cpp#L71)
- [x] `ntl::spin_lock`
  - [x] `ntl::spin_lock`
    [(tested)](../test/driver/src/ntl.cpp#L80)
  - [x] `ntl::unique_lock<ntl::spin_lock>`
    [(tested)](../test/driver/src/ntl.cpp#L99)
- [x] `ntl::resource`
  - [x] `ntl::resource`
    [(tested)](../test/driver/src/ntl.cpp#L117)
  - [x] `ntl::unique_lock<ntl::resource>`
    [(tested)](../test/driver/src/ntl.cpp#L156)
  - [x] `ntl::shared_lock<ntl::resource>`
    [(tested)](../test/driver/src/ntl.cpp#L179)
