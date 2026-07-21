#include <fltKernel.h>

#include <ntl/flt/all>

#include "communication.hpp"
#include "minifilter_sample.hpp"

#include <chrono>
#include <utility>

namespace crtsys_minifilter_sample {

namespace {
ntl::flt::communication_publisher sample_publisher;
}

std::uint64_t
publish_progress_callback(const ntl::flt::communication_context &context,
                          std::uint32_t percent, bool reliable) {
  const progress_event event{percent};
  if (!reliable) {
    const auto status = sample_publisher.try_notify(
        context.connection(), progress_notification, event);
    if (status.is_err())
      throw ntl::exception(status, "Failed to publish progress notification");
    return 0;
  }

  auto sequence = sample_publisher.try_notify(context.session_id(),
                                              progress_notification, event);
  if (!sequence)
    throw ntl::exception(sequence.status(),
                         "Failed to queue reliable progress notification");
  return *sequence;
}

std::uint32_t call_app_callback(const ntl::flt::communication_context &context,
                                std::uint32_t value) {
  auto response =
      sample_publisher.try_request(context.connection(), app_transform_method,
                                   std::chrono::seconds(2), value);
  if (!response)
    throw ntl::exception(response.status(), "Failed to call the app handler");
  return *response;
}

ntl::status add_communication_port(ntl::flt::driver &driver) {
  auto server = make_server();
  sample_publisher = server.publisher();
  server.register_client_method(app_transform_method)
      .on(call_app_method, call_app_callback);
  return driver.add_communication_port(port_name, std::move(server));
}

} // namespace crtsys_minifilter_sample
