#include "http3_origin_fallback.hpp"

#include <iostream>
#include <stdexcept>
#include <string>
#include <system_error>

namespace sample = crtsys::wfp_sample::browser_https;

namespace {

std::system_error transport_failure() {
  return {12030, std::system_category(), "transport"};
}

void require(bool value, const char *message) {
  if (!value)
    throw std::runtime_error(message);
}

} // namespace

int main() {
  const auto is_transport = [](const std::system_error &error) noexcept {
    return error.code().value() == 12030;
  };

  int h3_calls = 0;
  int fallback_calls = 0;
  const auto get_result = sample::fetch_with_safe_transport_fallback(
      "GET",
      [&]() -> std::string {
        ++h3_calls;
        throw transport_failure();
      },
      [&] {
        ++fallback_calls;
        return std::string("h2");
      },
      is_transport);
  require(get_result == "h2" && h3_calls == 1 && fallback_calls == 1,
          "safe GET transport failure did not fallback exactly once");

  bool post_failed = false;
  try {
    (void)sample::fetch_with_safe_transport_fallback(
        "POST",
        []() -> std::string { throw transport_failure(); },
        [&] {
          ++fallback_calls;
          return std::string("duplicate");
        },
        is_transport);
  } catch (const std::system_error &) {
    post_failed = true;
  }
  require(post_failed && fallback_calls == 1,
          "non-idempotent POST was replayed after a transport failure");

  bool validation_failed = false;
  try {
    (void)sample::fetch_with_safe_transport_fallback(
        "HEAD",
        []() -> std::string {
          throw std::system_error(
              87, std::system_category(), "validation");
        },
        [&] {
          ++fallback_calls;
          return std::string("unexpected");
        },
        is_transport);
  } catch (const std::system_error &) {
    validation_failed = true;
  }
  require(validation_failed && fallback_calls == 1,
          "non-transport failure incorrectly triggered fallback");

  const auto h3_result = sample::fetch_with_safe_transport_fallback(
      "OPTIONS", [] { return std::string("h3"); },
      [&] {
        ++fallback_calls;
        return std::string("unexpected");
      },
      is_transport);
  require(h3_result == "h3" && fallback_calls == 1,
          "successful HTTP/3 request incorrectly triggered fallback");

  require(sample::is_safe_origin_fallback_method("TRACE") &&
              !sample::is_safe_origin_fallback_method("get") &&
              !sample::is_safe_origin_fallback_method("trace") &&
              !sample::is_safe_origin_fallback_method("PUT") &&
              !sample::is_safe_origin_fallback_method("DELETE") &&
              !sample::is_safe_origin_fallback_method("PATCH"),
          "safe-method policy is incomplete");

  std::cout
      << "HTTP/3 origin fallback contracts passed: "
         "quic-transport=fallback "
         "non-idempotent-after-commit=no-retry "
         "lowercase-custom-method=no-retry "
         "validation=fail-closed h3-success=strict "
         "protocol=recorded\n";
  return 0;
}
