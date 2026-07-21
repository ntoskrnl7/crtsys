#include <cstdio>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include <ntl/rpc/client>

#include "examples.hpp"
#include "ntl_rpc_sample.hpp"

namespace crtsys_ntl_rpc_sample_app {

void run_streaming_batch() {
  ntl::rpc::client client(L"crtsys_ntl_rpc_sample");
  if (!client)
    throw std::runtime_error("could not open the RPC sample device");

  ntl::rpc::contract_requirements requirements;
  requirements
      .transport_features(ntl::rpc::transport_features::client_sessions |
                          ntl::rpc::transport_features::typed_streams |
                          ntl::rpc::transport_features::bounded_batches)
      .method(crtsys_ntl_rpc_sample::messages_stream.upload_method());
  (void)client.require_contract(requirements);

  (void)client.start_session();
  auto messages = crtsys_ntl_rpc_sample::messages(client);

  std::vector<ntl_rpc_sample_stream_upload> uploads;
  for (std::uint64_t sequence = 1; sequence <= 3; ++sequence) {
    ntl_rpc_sample_stream_upload upload;
    upload.sequence = sequence;
    upload.text = "batched chunk " + std::to_string(sequence);
    upload.finish = sequence == 3;
    uploads.push_back(std::move(upload));
  }
  messages.write_batch(uploads);

  bool completed = false;
  while (!completed) {
    // read_batch() returns the records that are ready now; it does not wait
    // for all four slots to fill.
    const auto batch = messages.read_batch(4);
    for (const auto &delivery : batch.values()) {
      const auto &record = delivery.payload();
      if (record.has_value()) {
        std::printf("stream batch: sequence=%llu text=%s\n",
                    static_cast<unsigned long long>(record.value().sequence),
                    record.value().text.c_str());
      } else if (record.is_completed()) {
        completed = true;
      } else {
        throw std::runtime_error("driver failed the batched stream");
      }
    }
    messages.acknowledge(batch);
  }

  messages.close();
  client.close_session();
}

} // namespace crtsys_ntl_rpc_sample_app
