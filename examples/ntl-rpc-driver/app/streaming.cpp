#include <cstdio>
#include <stdexcept>
#include <string>

#include <ntl/rpc/client>

#include "examples.hpp"
#include "ntl_rpc_sample.hpp"

namespace crtsys_ntl_rpc_sample_app {

void run_streaming() {
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

  // Keep one overlapped download read armed while the app sends each upload.
  // The two directions are independent: write() invokes the driver callback,
  // while read_async() waits for the next reliable driver-to-app record.
  auto pending_download = messages.read_async();
  constexpr std::uint64_t chunk_count = 3;
  for (std::uint64_t sequence = 1; sequence <= chunk_count; ++sequence) {
    ntl_rpc_sample_stream_upload upload;
    upload.sequence = sequence;
    upload.text = "chunk " + std::to_string(sequence);
    upload.finish = sequence == chunk_count;
    messages.write(upload);

    const auto delivery = pending_download.get();
    const auto &record = delivery.payload();
    if (!record.has_value()) {
      messages.acknowledge(delivery);
      throw std::runtime_error("driver ended the stream before its reply");
    }

    std::printf("stream: sequence=%llu text=%s\n",
                static_cast<unsigned long long>(record.value().sequence),
                record.value().text.c_str());
    messages.acknowledge(delivery);
    pending_download = messages.read_async();
  }

  const auto terminal = pending_download.get();
  if (terminal.payload().is_failed()) {
    messages.acknowledge(terminal);
    throw std::runtime_error("driver ended the stream with an error");
  }
  if (!terminal.payload().is_completed()) {
    messages.acknowledge(terminal);
    throw std::runtime_error("driver returned an unexpected stream record");
  }

  std::printf("stream: completed\n");
  messages.acknowledge(terminal);
  messages.close();
  client.close_session();
}

} // namespace crtsys_ntl_rpc_sample_app
