#pragma once

#include <cstddef>
#include <string>
#include <string_view>
#include <utility>

#include <ntl/net/http/inspection_conditions>
#include <ntl/net/http/inspection_policy>

namespace crtsys::examples::wfp::tls_inspection {

inline constexpr std::size_t maximum_http_body_size = 32 * 1024;
inline constexpr std::string_view inspection_header = "x-ntl-inspected";
inline constexpr std::string_view inspection_header_value = "1";
inline constexpr std::string_view response_marker =
    "<!-- inspected by ntl -->";
inline constexpr std::string_view blocked_response_body =
    "<html><body>blocked by NTL TLS policy</body></html>";

/**
 * The common semantic HTTP policy used by both the user- and kernel-runtime
 * TLS inspection examples.  Only the transport and execution location differ.
 */
inline ntl::net::http::inspection_policy make_inspection_policy() {
  ntl::net::http::inspection_policy policy(
      {.maximum_header_count = 128,
       .maximum_header_bytes = 16 * 1024,
       .maximum_encoded_body_bytes = maximum_http_body_size,
       .maximum_decoded_body_bytes = maximum_http_body_size,
       .maximum_expansion_ratio = 16,
       .maximum_coding_layers = 2,
       .on_failure = ntl::net::http::transform_failure_policy::block});

  policy.transforms_ref().requests().transform(
      [](ntl::net::http::request_message &request) {
        request.headers.set(std::string(inspection_header),
                            std::string(inspection_header_value));
        return ntl::net::http::rewrite_result::headers_changed();
      });

  namespace condition = ntl::net::http::condition;
  policy.requests()
      .at_message_complete()
      .when(condition::all_of(
          condition::path_is("/inspect"),
          condition::any_of(
              condition::header_is("x-ntl-block", "1"),
              condition::complete_body_contains("BLOCKME"))))
      .decide([](const ntl::net::http::inspection_context_view &) {
        return ntl::net::inspection::verdict::block;
      });

  policy.transforms_ref().responses().html().transform(
      [](const ntl::net::http::request_message &,
         ntl::net::http::response_message &response) {
        response.body.insert(
            response.body.end(),
            reinterpret_cast<const std::byte *>(response_marker.data()),
            reinterpret_cast<const std::byte *>(response_marker.data() +
                                                 response_marker.size()));
        return ntl::net::http::rewrite_result::replace_body(
            std::move(response.body),
            ntl::net::http::transformed_body_coding::identity);
      });
  return policy;
}

} // namespace crtsys::examples::wfp::tls_inspection
