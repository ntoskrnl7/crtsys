# Feature Coverage

[Back to README](../README.md)

This page keeps the detailed support checklist that used to live in the main
README. Checked items are implemented and have test coverage when a `tested`
link is attached.

Coverage here means that the feature is implemented and exercised by the test
suite. It does not mean the feature is valid in every driver execution context.
Unless an API documents a broader contract, assume `PASSIVE_LEVEL`. See
[Design Rationale and Operational Boundaries](./design-rationale.md) and the
[NTL API reference](./ntl-api.md) for execution-context guidance.

## C++ Standard

The tests are based on examples and behavior described by
[cppreference](https://en.cppreference.com).

### Initialization

- [x] [Non-local variables](https://en.cppreference.com/w/cpp/language/initialization#Non-local_variables)
  - [x] [Static initialization](https://en.cppreference.com/w/cpp/language/initialization#Static_initialization)
    - [x] [Constant initialization](https://en.cppreference.com/w/cpp/language/constant_initialization)
      [(tested)](../test/cmake/driver/src/cpp/lang/initialization.cpp#L13)
    - [x] [Zero initialization](https://en.cppreference.com/w/cpp/language/zero_initialization)
      [(tested)](../test/cmake/driver/src/cpp/lang/initialization.cpp#L41)
  - [x] [Dynamic initialization](https://en.cppreference.com/w/cpp/language/initialization#Dynamic_initialization)
    [(tested)](../test/cmake/driver/src/cpp/lang/initialization.cpp#L65)
- [ ] [Static local variables](https://en.cppreference.com/w/cpp/language/storage_duration#Static_local_variables)
  - [ ] `thread_local`
  - [ ] function-local `static`

### Exceptions

- [x] [throw](https://en.cppreference.com/w/cpp/language/throw)
  [(tested)](../test/cmake/driver/src/cpp/lang/exceptions.cpp#L42)
- [x] [try block](https://en.cppreference.com/w/cpp/language/try_catch)
  [(tested)](../test/cmake/driver/src/cpp/lang/exceptions.cpp#L60)
- [x] [Function try block](https://en.cppreference.com/w/cpp/language/function-try-block)
  [(tested)](../test/cmake/driver/src/cpp/lang/exceptions.cpp#L98)

## Microsoft STL

- [x] [std::chrono](https://en.cppreference.com/w/cpp/chrono)
  [(tested)](../test/cmake/driver/src/cpp/stl/chrono.cpp#L15)
- [x] [std::thread](https://en.cppreference.com/w/cpp/thread)
  [(tested)](../test/cmake/driver/src/cpp/stl/thread.cpp#L35)
- [x] [std::condition_variable](https://en.cppreference.com/w/cpp/thread/condition_variable)
  [(tested)](../test/cmake/driver/src/cpp/stl/thread.cpp#L35)
- [x] [std::mutex](https://en.cppreference.com/w/cpp/thread/mutex)
  [(tested)](../test/cmake/driver/src/cpp/stl/thread.cpp#L81)
- [x] [std::shared_mutex](https://en.cppreference.com/w/cpp/thread/shared_mutex)
  [(tested)](../test/cmake/driver/src/cpp/stl/thread.cpp#L129)
- [x] [std::future](https://en.cppreference.com/w/cpp/thread/future)
  [(tested)](../test/cmake/driver/src/cpp/stl/thread.cpp#L157)
- [x] [std::promise](https://en.cppreference.com/w/cpp/thread/promise)
  [(tested)](../test/cmake/driver/src/cpp/stl/thread.cpp#L203)
- [x] [std::packaged_task](https://en.cppreference.com/w/cpp/thread/packaged_task)
  [(tested)](../test/cmake/driver/src/cpp/stl/thread.cpp#L267)
- [x] [std::cin](https://en.cppreference.com/w/cpp/io/cin)
- [x] [std::cout](https://en.cppreference.com/w/cpp/io/cout)
- [x] [std::cerr](https://en.cppreference.com/w/cpp/io/cerr)
- [x] [std::clog](https://en.cppreference.com/w/cpp/io/clog)
- [x] [std::wcin](https://en.cppreference.com/w/cpp/io/cin)
- [x] [std::wcout](https://en.cppreference.com/w/cpp/io/cout)
- [x] [std::wcerr](https://en.cppreference.com/w/cpp/io/cerr)
- [x] [std::wclog](https://en.cppreference.com/w/cpp/io/clog)
- [x] `nlohmann::json` integration
  [(tested)](../test/cmake/driver/src/libs/nlohmann_json.cpp)

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
  [(tested)](../test/cmake/driver/src/ntl.cpp#L6)
- [x] `ntl::status`
- [x] `ntl::driver`
  - [x] unload callback
    [(tested)](../test/cmake/driver/src/main.cpp#L73)
  - [x] device creation
    [(tested)](../test/cmake/driver/src/main.cpp#L44)
- [x] `ntl::device`
  - [x] device extension
    [(tested)](../test/cmake/driver/src/main.cpp#L33)
  - [ ] `IRP_MJ_CREATE`
  - [ ] `IRP_MJ_CLOSE`
  - [x] `IRP_MJ_DEVICE_CONTROL`
    [(app test)](../test/cmake/app/src/main.cpp#L77)
    [(driver test)](../test/cmake/driver/src/main.cpp#L55)
- [x] `ntl::main`
  [(tested)](../test/cmake/driver/src/main.cpp#L25)
- [x] `ntl::rpc`
  - [x] `ntl::rpc::server`
    [(tested)](../test/cmake/driver/src/main.cpp#L19)
    [(schema)](../test/cmake/common/rpc.hpp)
  - [x] `ntl::rpc::client`
    [(tested)](../test/cmake/app/src/main.cpp#L4)
    [(schema)](../test/cmake/common/rpc.hpp)
- [x] `ntl::irql`
  - [x] `ntl::irql`
    [(tested)](../test/cmake/driver/src/ntl.cpp#L47)
  - [x] `ntl::raise_irql`
    [(tested)](../test/cmake/driver/src/ntl.cpp#L50)
  - [x] `ntl::raise_irql_to_dpc_level`
    [(tested)](../test/cmake/driver/src/ntl.cpp#L63)
  - [x] `ntl::raise_irql_to_synch_level`
    [(tested)](../test/cmake/driver/src/ntl.cpp#L72)
- [x] `ntl::spin_lock`
  - [x] `ntl::spin_lock`
    [(tested)](../test/cmake/driver/src/ntl.cpp#L81)
  - [x] `ntl::unique_lock<ntl::spin_lock>`
    [(normal lock tested)](../test/cmake/driver/src/ntl.cpp#L94)
    [(at-DPC tested)](../test/cmake/driver/src/ntl.cpp#L140)
- [x] `ntl::resource`
  - [x] `ntl::resource`
    [(tested)](../test/cmake/driver/src/ntl.cpp#L181)
  - [x] `ntl::unique_lock<ntl::resource>`
    [(tested)](../test/cmake/driver/src/ntl.cpp#L220)
  - [x] `ntl::shared_lock<ntl::resource>`
    [(tested)](../test/cmake/driver/src/ntl.cpp#L230)
