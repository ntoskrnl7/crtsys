#include "communication.hpp"
#include "coroutine_task.hpp"
#include "minifilter_sample.hpp"

#include <ntl/flt/coroutine>

#include <cstdint>

namespace crtsys_minifilter_sample {
namespace {

coroutine_task<std::uint32_t>
ping_with_coroutine(ntl::flt::communication_client &client) {
  co_return co_await ping_async(client, std::uint32_t{41});
}

coroutine_task<progress_event>
progress_with_coroutine(ntl::flt::communication_client &client) {
  co_return co_await progress_async(client);
}

using number_record = ntl::rpc::stream_record<std::uint32_t>;
using number_delivery = ntl::rpc::notification_delivery<number_record>;

coroutine_task<number_delivery> number_with_coroutine(
    ntl::flt::communication_client_stream<numbers_stream_type> &stream) {
  co_return co_await stream.read_async();
}

} // namespace

std::uint32_t
run_coroutine_communication_sample(ntl::flt::communication_client &client) {
  auto task = ping_with_coroutine(client);
  return task.get();
}

std::uint32_t
run_coroutine_notification_sample(ntl::flt::communication_client &client) {
  auto task = progress_with_coroutine(client);
  (void)publish_progress(client, std::uint32_t{50}, false);
  return task.get().percent;
}

std::uint32_t
run_coroutine_stream_sample(ntl::flt::communication_client &client) {
  auto stream = numbers(client);
  auto task = number_with_coroutine(stream);
  stream.write(std::uint32_t{42});
  const auto delivery = task.get();
  if (!delivery.payload().has_value())
    throw std::runtime_error("Minifilter coroutine stream returned no value");
  const auto value = delivery.payload().value();
  stream.acknowledge(delivery);
  stream.close();
  return value;
}

} // namespace crtsys_minifilter_sample
