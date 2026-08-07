#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <windows.h>

#include <ntl/net/user/redirected_tls_inspection>
#include <ntl/net/http/inspection_policy>

#include <cstddef>
#include <coroutine>
#include <exception>
#include <iostream>
#include <iterator>
#include <optional>
#include <stdexcept>
#include <type_traits>
#include <vector>

namespace {

template <class T> class immediate_task {
public:
  struct promise_type;
  using handle_type = std::coroutine_handle<promise_type>;

  explicit immediate_task(handle_type handle) noexcept : handle_(handle) {}
  immediate_task(const immediate_task &) = delete;
  immediate_task &operator=(const immediate_task &) = delete;
  immediate_task(immediate_task &&other) noexcept
      : handle_(std::exchange(other.handle_, {})) {}
  ~immediate_task() {
    if (handle_)
      handle_.destroy();
  }

  T get() {
    if (!handle_ || !handle_.done())
      throw std::logic_error("immediate task did not complete synchronously");
    if (handle_.promise().exception)
      std::rethrow_exception(handle_.promise().exception);
    return std::move(*handle_.promise().value);
  }

  struct promise_type {
    immediate_task get_return_object() noexcept {
      return immediate_task(handle_type::from_promise(*this));
    }
    std::suspend_never initial_suspend() const noexcept { return {}; }
    std::suspend_always final_suspend() const noexcept { return {}; }
    template <class U> void return_value(U &&result) {
      value.emplace(std::forward<U>(result));
    }
    void unhandled_exception() noexcept {
      exception = std::current_exception();
    }

    std::optional<T> value;
    std::exception_ptr exception;
  };

private:
  handle_type handle_{};
};

immediate_task<ntl::net::user::inspected_http_session_summary>
complete_immediately(
    ntl::net::user::task<ntl::net::user::inspected_http_session_summary>
        operation) {
  co_return co_await std::move(operation);
}

class compile_dispatcher final
    : public ntl::net::user::redirected_tls_http_dispatcher {
public:
  ntl::net::user::task<ntl::net::user::inspected_http_session_summary> run(
      ntl::net::user::inspected_http_protocol,
      std::shared_ptr<ntl::net::tls_stream>,
      std::shared_ptr<ntl::net::tls_stream>,
      const ntl::net::http::inspection_session_metadata &) override {
    co_return ntl::net::user::inspected_http_session_summary{};
  }
};

void require(bool condition, const char *message) {
  if (!condition)
    throw std::runtime_error(message);
}

} // namespace

int main() {
  try {
    static_assert(std::is_abstract_v<
                  ntl::net::user::redirected_tls_http_dispatcher>);
    static_assert(!std::is_copy_constructible_v<
                  ntl::net::user::redirected_tls_session>);
    compile_dispatcher dispatcher;
    (void)dispatcher;

    auto policy =
        std::make_shared<ntl::net::http::inspection_policy>();
    auto decoders = std::make_shared<
        ntl::net::inspection::content_decoder_registry>();
    auto encoders = std::make_shared<
        ntl::net::inspection::content_encoder_registry>();
    policy->use_content_codecs(decoders, encoders);
    ntl::net::user::standard_redirected_tls_inspection standard(
        policy);
    standard.close();
    standard.close();
    require(standard.is_closed(), "duplicate close did not remain closed");
    bool rejected_after_close = false;
    try {
      (void)complete_immediately(standard.run(
                                     ntl::net::user::inspected_http_protocol::
                                         http1,
                                     {}, {}, {}))
          .get();
    } catch (const std::system_error &error) {
      rejected_after_close =
          error.code() == std::error_code(ERROR_OPERATION_ABORTED,
                                          std::system_category());
    }
    require(rejected_after_close,
            "closed inspection facade accepted a new session");

    std::optional<ntl::net::user::task<
        ntl::net::user::inspected_http_session_summary>> deferred;
    {
      ntl::net::user::standard_redirected_tls_inspection temporary(
          policy);
      deferred.emplace(temporary.run(
          ntl::net::user::inspected_http_protocol::http1, {}, {}, {}));
    }
    // The lazy task owns its policy/codec/factory snapshot. Destroying it
    // unawaited after the dispatcher facade has gone away must be harmless.
    deferred.reset();

    ntl::net::user::redirected_tls_session_limits limits;
    require(limits.valid(), "default TLS session bounds are invalid");
    limits.client_hello.maximum_client_hello =
        limits.client_hello.maximum_buffered_ciphertext + 1;
    require(!limits.valid(), "unbounded ClientHello was accepted");

    ntl::net::http::inspection_session_metadata metadata;
    const std::byte opaque_id[]{std::byte{0}, std::byte{0xff}, std::byte{1}};
    metadata.connection.connection_id = 1;
    metadata.connection.process_id = 42;
    metadata.connection.application_id =
        std::vector<std::byte>(std::begin(opaque_id), std::end(opaque_id));
    metadata.connection.application_label = "optional display label";
    require(metadata.connection.application_id &&
                metadata.connection.application_id->size() == 3 &&
                (*metadata.connection.application_id)[1] == std::byte{0xff} &&
                metadata.connection.application_label ==
                    "optional display label",
            "opaque application identity was conflated with display text");

    std::cout << "redirected TLS session contracts passed\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "redirected TLS session contracts failed: " << error.what()
              << '\n';
    return 1;
  }
}
