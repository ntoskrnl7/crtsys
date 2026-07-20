#include <cstdio>
#include <stdexcept>

#include <ntl/rpc/client>

#include "examples.hpp"
#include "ntl_rpc_sample.hpp"

namespace crtsys_ntl_rpc_sample_app {
namespace {

ntl::rpc::client open_session_client() {
  ntl::rpc::client client(L"crtsys_ntl_rpc_sample");
  if (!client)
    throw std::runtime_error("could not open the RPC sample device");

  ntl::rpc::contract_requirements requirements;
  requirements
      .transport_features(ntl::rpc::transport_features::client_sessions |
                          ntl::rpc::transport_features::
                              reliable_notifications)
      .method(crtsys_ntl_rpc_sample::publish_progress);
  (void)client.require_contract(requirements);
  return client;
}

} // namespace

void run_reliable_notifications() {
  ntl::rpc::session_token token{};
  {
    auto client = open_session_client();
    token = client.start_session().token;
    client.subscribe(crtsys_ntl_rpc_sample::progress);
    if (!client.invoke(crtsys_ntl_rpc_sample::publish_progress, 42u))
      throw std::runtime_error("reliable notification publish failed");

    const auto delivery =
        client.receive_reliable(crtsys_ntl_rpc_sample::progress);
    std::printf("reliable notification: sequence=%llu text=%s\n",
                static_cast<unsigned long long>(delivery.sequence()),
                delivery.payload().text.c_str());
    // Leaving this delivery unacknowledged makes it replay after reconnect.
  }

  auto resumed = open_session_client();
  (void)resumed.resume_session(token);
  const auto replay =
      resumed.receive_reliable(crtsys_ntl_rpc_sample::progress);
  resumed.acknowledge(crtsys_ntl_rpc_sample::progress, replay);
  resumed.close_session();
}

} // namespace crtsys_ntl_rpc_sample_app
