#include "communication_advanced.hpp"

#include "../shared/runtime_test.hpp"

#include <ntl/flt/communication_client>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <thread>
#include <vector>

namespace crtsys_flt_runtime_test {
namespace {

template <typename Wait> bool completes(Wait &wait) {
  return wait.wait_for(std::chrono::seconds(5)) ==
         ntl::flt::communication_wait_status::completed;
}

bool test_contract(ntl::flt::communication_client &client) {
  constexpr std::uint64_t features =
      ntl::rpc::transport_features::bidirectional_requests |
      ntl::rpc::transport_features::connection_lifecycle |
      ntl::rpc::transport_features::detailed_contract |
      ntl::rpc::transport_features::notification_persistence_hooks;
  const auto contract = client.query_contract();
  if ((contract.transport_feature_mask() & features) != features ||
      !contract.supports(ping_method) ||
      !contract.supports_client_method(client_transform_method) ||
      !contract.supports(progress_notification) ||
      !contract.supports(numbers_stream))
    return false;

  client.require_method(ping_method);
  client.require_client_method(client_transform_method);
  client.require_notification(progress_notification);
  client.require_stream(numbers_stream);

  bool version_mismatch_rejected = false;
  try {
    client.require_contract(contract_version + 1);
  } catch (const std::runtime_error &) {
    version_mismatch_rejected = true;
  }

  bool mismatch_rejected = false;
  try {
    constexpr auto incompatible = ping_method.max_request_size<128>();
    client.require_method(incompatible);
  } catch (const std::runtime_error &) {
    mismatch_rejected = true;
  }
  return version_mismatch_rejected && mismatch_rejected;
}

bool test_connection_and_bidirectional_request(
    ntl::flt::communication_client &client) {
  client.on_request(client_transform_method,
                    [](std::uint32_t value) noexcept { return value * 3; });

  const auto first = client.invoke(connection_state_method);
  const auto second = client.invoke(connection_state_method);
  if ((first >> 32) == 0 || (first >> 32) != (second >> 32) ||
      static_cast<std::uint32_t>(first) != 1 ||
      static_cast<std::uint32_t>(second) != 2)
    return false;

  if (client.invoke(request_client_method, std::uint32_t{14}) != 42)
    return false;

  bool unsubscribed_target_rejected = false;
  try {
    (void)client.invoke(publish_targeted_method, std::uint32_t{0xA57});
  } catch (const ntl::flt::communication_error &) {
    unsubscribed_target_rejected = true;
  }
  if (!unsubscribed_target_rejected)
    return false;

  auto targeted = client.receive_async(progress_notification);
  const auto target_id =
      client.invoke(publish_targeted_method, std::uint32_t{0xA58});
  if (!completes(targeted))
    return false;
  const auto delivered = targeted.get();
  if (target_id != (first >> 32) || delivered.value != 0xA58)
    return false;

  using wait_type = decltype(client.receive_async(progress_notification));
  std::vector<wait_type> waits;
  for (std::uint32_t value = 0; value != 8; ++value)
    waits.push_back(client.receive_async(progress_notification));
  for (std::size_t index = 0; index != waits.size(); ++index)
    (void)client.invoke(publish_targeted_method,
                        0xB000u + static_cast<std::uint32_t>(index));
  std::vector<std::uint32_t> received;
  for (auto &wait : waits) {
    if (!completes(wait))
      return false;
    received.push_back(wait.get().value);
  }
  std::sort(received.begin(), received.end());
  for (std::size_t index = 0; index != received.size(); ++index)
    if (received[index] != 0xB000u + static_cast<std::uint32_t>(index))
      return false;
  return true;
}

bool test_reliable_batch() {
  auto target = connect();
  auto subscription =
      target.receive_reliable_batch_async(background_progress_notification);
  (void)subscription.cancel();
  const auto session_id = target.session().id;
  const auto token = target.preserve_session();

  {
    auto sender = connect();
    for (std::uint32_t value = 100; value != 103; ++value) {
      const auto sequence =
          sender.invoke(publish_priority_method, session_id, value, false);
      if (sequence == 0)
        return false;
    }
  }

  auto resumed = resume(token);
  auto batch_wait =
      resumed.receive_reliable_batch_async(background_progress_notification);
  if (!completes(batch_wait))
    return false;
  const auto batch = batch_wait.get();
  if (batch.size() != 3)
    return false;
  for (std::size_t index = 0; index != batch.size(); ++index) {
    if (batch.values()[index].payload().value != 100 + index)
      return false;
  }
  resumed.acknowledge(background_progress_notification, batch);

  bool duplicate_ack_rejected = false;
  try {
    resumed.acknowledge(background_progress_notification, batch);
  } catch (const ntl::flt::communication_error &) {
    duplicate_ack_rejected = true;
  }
  bool unknown_sequence_rejected = false;
  try {
    resumed.acknowledge(
        background_progress_notification,
        ntl::rpc::notification_delivery<progress_event>{
            (std::numeric_limits<std::uint64_t>::max)(), progress_event{}});
  } catch (const ntl::flt::communication_error &) {
    unknown_sequence_rejected = true;
  }
  return duplicate_ack_rejected && unknown_sequence_rejected;
}

bool test_restored_notification() {
  auto restored =
      ntl::flt::communication_client::resume(port_name, restored_session_token);
  auto delivery_wait = restored.receive_reliable_async(progress_notification);
  if (!completes(delivery_wait))
    return false;
  const auto delivery = delivery_wait.get();
  if (delivery.sequence() != 1 || delivery.payload().value != 0x52455354)
    return false;
  restored.acknowledge(progress_notification, delivery);
  const auto stats = restored.invoke(storage_stats_method);
  const auto acknowledged = (stats >> 21) & 0x1fffff;
  const auto restored_count = stats & 0x1fffff;
  return acknowledged != 0 && restored_count != 0;
}

bool test_drop_oldest_policy() {
  auto target = ntl::flt::communication_client::connect(drop_port_name);
  auto subscription =
      target.receive_reliable_async(background_progress_notification);
  (void)subscription.cancel();
  const auto session_id = target.session().id;
  const auto token = target.preserve_session();

  {
    auto sender = ntl::flt::communication_client::connect(drop_port_name);
    for (std::uint32_t value = 1; value != 4; ++value) {
      if (sender.invoke(drop_publish_method, session_id, value) == 0)
        return false;
    }
  }

  auto resumed = ntl::flt::communication_client::resume(drop_port_name, token);
  auto second_wait =
      resumed.receive_reliable_async(background_progress_notification);
  if (!completes(second_wait))
    return false;
  const auto second = second_wait.get();
  if (second.payload().value != 2)
    return false;
  resumed.acknowledge(background_progress_notification, second);
  auto third_wait =
      resumed.receive_reliable_async(background_progress_notification);
  if (!completes(third_wait))
    return false;
  const auto third = third_wait.get();
  if (third.payload().value != 3)
    return false;
  resumed.acknowledge(background_progress_notification, third);
  return true;
}

bool test_reliable_byte_quota() {
  auto target = ntl::flt::communication_client::connect(byte_quota_port_name);
  auto cancelled_subscription =
      target.receive_reliable_async(background_progress_notification);
  (void)cancelled_subscription.cancel();
  const auto session_id = target.session().id;

  auto sender =
      ntl::flt::communication_client::connect(byte_quota_port_name);
  if (sender.invoke(drop_publish_method, session_id, std::uint32_t{1}) == 0 ||
      sender.invoke(drop_publish_method, session_id, std::uint32_t{2}) == 0)
    return false;

  bool quota_rejected = false;
  try {
    (void)sender.invoke(drop_publish_method, session_id, std::uint32_t{3});
  } catch (const ntl::flt::communication_error &) {
    quota_rejected = true;
  }
  if (!quota_rejected)
    return false;

  auto first = target.receive_reliable(background_progress_notification);
  target.acknowledge(background_progress_notification, first);
  auto second = target.receive_reliable(background_progress_notification);
  if (first.payload().value != 1 || second.payload().value != 2)
    return false;
  target.acknowledge(background_progress_notification, second);
  return true;
}

bool run_connection_stress() {
  std::atomic<std::uint32_t> failures{0};
  std::vector<std::thread> workers;
  for (std::uint32_t worker = 0; worker != 4; ++worker) {
    workers.emplace_back([worker, &failures] {
      for (std::uint32_t iteration = 0; iteration != 16; ++iteration) {
        try {
          auto client = connect();
          const auto value = worker * 100 + iteration;
          if (ping(client, value) != value + 1)
            failures.fetch_add(1, std::memory_order_relaxed);
        } catch (...) {
          failures.fetch_add(1, std::memory_order_relaxed);
        }
      }
    });
  }
  for (auto &worker : workers)
    worker.join();
  if (failures.load(std::memory_order_relaxed) != 0)
    return false;

  auto observer = connect();
  const auto stats = observer.invoke(connection_lifecycle_stats_method);
  const auto connected = static_cast<std::uint32_t>(stats >> 32);
  const auto disconnected = static_cast<std::uint32_t>(stats);
  return connected != 0 && disconnected != 0 && connected > disconnected;
}

} // namespace

bool run_advanced_communication_tests(ntl::flt::communication_client &client) {
  if (!test_contract(client)) {
    std::cerr << "minifilter detailed contract validation failed\n";
    return false;
  }
  if (!test_connection_and_bidirectional_request(client)) {
    std::cerr << "minifilter connection or bidirectional request failed\n";
    return false;
  }
  if (!test_reliable_batch()) {
    std::cerr << "minifilter reliable batch validation failed\n";
    return false;
  }
  if (!test_restored_notification()) {
    std::cerr << "minifilter notification restore hook failed\n";
    return false;
  }
  if (!test_drop_oldest_policy()) {
    std::cerr << "minifilter drop-oldest queue policy failed\n";
    return false;
  }
  if (!test_reliable_byte_quota()) {
    std::cerr << "minifilter reliable byte quota failed\n";
    return false;
  }
  if (!run_connection_stress()) {
    std::cerr << "minifilter connection lifecycle stress failed\n";
    return false;
  }
  return true;
}

} // namespace crtsys_flt_runtime_test
