#pragma once

#include <functional>
#include <string_view>
#include <system_error>
#include <type_traits>
#include <utility>

namespace crtsys::wfp_sample::browser_https {

// A transport-level retry can happen after WinHTTP has placed request bytes on
// the wire. Limit automatic fallback to methods whose semantics are defined as
// safe; a POST/PATCH/PUT/DELETE is never replayed implicitly.
inline bool is_safe_origin_fallback_method(
    std::string_view method) noexcept {
  return method == "GET" || method == "HEAD" ||
         method == "OPTIONS" || method == "TRACE";
}

template <class Http3Fetch, class TlsTcpFetch,
          class IsTransportFailure>
auto fetch_with_safe_transport_fallback(
    std::string_view method, Http3Fetch &&http3_fetch,
    TlsTcpFetch &&tls_tcp_fetch,
    IsTransportFailure &&is_transport_failure)
    -> std::invoke_result_t<Http3Fetch &> {
  using result_type = std::invoke_result_t<Http3Fetch &>;
  static_assert(!std::is_void_v<result_type>);
  static_assert(std::is_same_v<
                result_type, std::invoke_result_t<TlsTcpFetch &>>);

  try {
    return std::invoke(http3_fetch);
  } catch (const std::system_error &error) {
    if (!is_safe_origin_fallback_method(method) ||
        !std::invoke(is_transport_failure, error))
      throw;
  }
  return std::invoke(tls_tcp_fetch);
}

} // namespace crtsys::wfp_sample::browser_https
