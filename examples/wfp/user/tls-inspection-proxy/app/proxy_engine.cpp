#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <windows.h>

#include "proxy_engine.hpp"

#include <algorithm>
#include <cstddef>
#include <cstring>
#include <mutex>
#include <string_view>
#include <unordered_map>
#include <utility>

#include "inspection_policy.hpp"
#include "tls_inspection_policy.hpp"

namespace crtsys::wfp_sample::tls_inspection {
namespace {

bool contains(std::span<const std::byte> bytes,
              std::string_view text) noexcept {
  if (text.empty())
    return true;
  if (bytes.size() < text.size())
    return false;
  return std::search(
             bytes.begin(), bytes.end(),
             reinterpret_cast<const std::byte *>(text.data()),
             reinterpret_cast<const std::byte *>(text.data() + text.size())) !=
         bytes.end();
}

} // namespace

struct proxy_observation_state final
    : ntl::net::user::redirected_http_inspection_observer {
  void on_inspection(
      const ntl::net::http::inspection_context_view &context) noexcept override {
    try {
      if (context.stage() !=
          ntl::net::http::inspection_stage::message_complete)
        return;
      if (!context.connection().connection_id)
        return;
      std::lock_guard lock(observations_lock);
      auto &observation = observations[*context.connection().connection_id];
      if (context.direction() ==
          ntl::net::http::message_direction::request) {
        observation.request_transformed =
            context.request().headers.first(
                crtsys::examples::wfp::tls_inspection::inspection_header) ==
            crtsys::examples::wfp::tls_inspection::inspection_header_value;
      } else if (context.response()) {
        observation.response_transformed =
            contains(context.response()->body,
                     crtsys::examples::wfp::tls_inspection::response_marker);
      }
    } catch (...) {
      // Example telemetry cannot alter the inspection verdict.
    }
  }

  proxy_transform_observation take(std::uint64_t connection_id) noexcept {
    try {
      std::lock_guard lock(observations_lock);
      const auto found = observations.find(connection_id);
      if (found == observations.end())
        return {};
      const auto result = found->second;
      observations.erase(found);
      return result;
    } catch (...) {
      return {};
    }
  }

  std::mutex observations_lock;
  std::unordered_map<std::uint64_t, proxy_transform_observation> observations;
};

struct proxy_policy_runtime::implementation final {
  implementation() {
    policy = std::make_shared<ntl::net::http::inspection_policy>(
        make_inspection_policy());
    auto decoders = std::make_shared<
        ntl::net::inspection::content_decoder_registry>();
    auto encoders = std::make_shared<
        ntl::net::inspection::content_encoder_registry>();
    policy->use_content_codecs(decoders, encoders);
    observations = std::make_shared<proxy_observation_state>();
    dispatcher = std::make_shared<
        ntl::net::user::standard_redirected_tls_inspection>(
        policy, ntl::net::user::redirected_tls_inspection_options{
                    .make_observer =
                        [observer = observations](const auto &) {
                          return observer;
                        }});
  }

  std::shared_ptr<ntl::net::http::inspection_policy> policy;
  std::shared_ptr<proxy_observation_state> observations;
  std::shared_ptr<ntl::net::user::standard_redirected_tls_inspection>
      dispatcher;
};

proxy_policy_runtime::proxy_policy_runtime()
    : implementation_(std::make_unique<implementation>()) {}

proxy_policy_runtime::~proxy_policy_runtime() = default;

std::shared_ptr<ntl::net::user::redirected_tls_http_dispatcher>
proxy_policy_runtime::dispatcher() const noexcept {
  return implementation_->dispatcher;
}

proxy_transform_observation
proxy_policy_runtime::take(std::uint64_t connection_id) noexcept {
  return implementation_->observations->take(connection_id);
}

proxy_connection_result make_proxy_connection_result(
    const ntl::net::user::redirected_tls_session_result &session,
    proxy_transform_observation observation) noexcept {
  proxy_connection_result result;
  result.address_family = session.connection.address_family;
  result.original_port = session.connection.original_destination.port;
  result.process_id = session.connection.process_id;
  result.application_id_size = session.connection.application_id.size();
  result.server_name = session.server_name;
  result.protocol =
      session.protocol == ntl::net::user::inspected_http_protocol::http2
          ? proxy_protocol::http2
          : proxy_protocol::http1;
  result.action = session.http.blocked || session.http.dropped
                      ? proxy_action::blocked
                      : proxy_action::permitted;
  if (result.action == proxy_action::permitted) {
    result.request_transformed = observation.request_transformed;
    result.response_transformed = observation.response_transformed;
  }
  return result;
}

} // namespace crtsys::wfp_sample::tls_inspection
