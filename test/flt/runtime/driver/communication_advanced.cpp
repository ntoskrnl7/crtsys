#include "communication_advanced.hpp"

#include "../shared/runtime_test.hpp"

#include <ntl/flt/driver>
#include <ntl/flt/registry_notification_store>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <memory>
#include <mutex>
#include <utility>
#include <vector>

namespace crtsys_flt_runtime_test {
namespace {

constexpr wchar_t notification_store_key[] =
    L"\\Registry\\Machine\\Software\\CrtSysFltNotificationStoreTest";

void delete_notification_store_key() noexcept {
  auto key = ntl::registry_key::open(notification_store_key, DELETE);
  if (!key)
    return;
  (void)key->delete_key();
}

struct restored_record {
  std::uint32_t channel_id = 0;
  std::uint64_t sequence = 0;
  ntl::flt::communication_record_kind kind =
      ntl::flt::communication_record_kind::notification;
  std::vector<unsigned char> payload;
};

class test_restore_sink final : public ntl::flt::communication_restore_sink {
public:
  ntl::status add(std::uint32_t channel_id, std::uint64_t sequence,
                  ntl::flt::communication_record_kind kind, const void *data,
                  std::size_t size) noexcept override {
    if (channel_id == 0 || sequence == 0 || (!data && size != 0))
      return ntl::status{STATUS_INVALID_PARAMETER};
    try {
      restored_record record;
      record.channel_id = channel_id;
      record.sequence = sequence;
      record.kind = kind;
      const auto *bytes = static_cast<const unsigned char *>(data);
      if (size != 0)
        record.payload.assign(bytes, bytes + size);
      records.push_back(std::move(record));
      return ntl::status::ok();
    } catch (const std::bad_alloc &) {
      return ntl::status{STATUS_INSUFFICIENT_RESOURCES};
    }
  }

  std::vector<restored_record> records;
};

struct port_security_descriptors {
  NTSTATUS initialize() noexcept {
    auto *const allow = reinterpret_cast<PSECURITY_DESCRIPTOR>(allow_storage);
    auto *const deny = reinterpret_cast<PSECURITY_DESCRIPTOR>(deny_storage);
    auto *const administrators =
        reinterpret_cast<PSECURITY_DESCRIPTOR>(administrators_storage);
    SID_IDENTIFIER_AUTHORITY nt_authority = SECURITY_NT_AUTHORITY;

    NTSTATUS status =
        RtlCreateSecurityDescriptor(allow, SECURITY_DESCRIPTOR_REVISION);
    if (!NT_SUCCESS(status))
      return status;
    status = RtlSetDaclSecurityDescriptor(allow, TRUE, nullptr, FALSE);
    if (!NT_SUCCESS(status))
      return status;

    status = RtlInitializeSid(
        reinterpret_cast<PSID>(administrators_sid_storage),
        &nt_authority, 2);
    if (!NT_SUCCESS(status))
      return status;
    *RtlSubAuthoritySid(reinterpret_cast<PSID>(administrators_sid_storage),
                        0) = SECURITY_BUILTIN_DOMAIN_RID;
    *RtlSubAuthoritySid(reinterpret_cast<PSID>(administrators_sid_storage),
                        1) = DOMAIN_ALIAS_RID_ADMINS;

    status = RtlCreateSecurityDescriptor(administrators,
                                          SECURITY_DESCRIPTOR_REVISION);
    if (!NT_SUCCESS(status))
      return status;
    status = RtlCreateAcl(reinterpret_cast<PACL>(administrators_acl_storage),
                          sizeof(administrators_acl_storage), ACL_REVISION);
    if (!NT_SUCCESS(status))
      return status;
    status = RtlAddAccessAllowedAce(
        reinterpret_cast<PACL>(administrators_acl_storage), ACL_REVISION,
        FLT_PORT_ALL_ACCESS,
        reinterpret_cast<PSID>(administrators_sid_storage));
    if (!NT_SUCCESS(status))
      return status;
    status = RtlSetDaclSecurityDescriptor(
        administrators, TRUE,
        reinterpret_cast<PACL>(administrators_acl_storage), FALSE);
    if (!NT_SUCCESS(status))
      return status;

    status = RtlCreateSecurityDescriptor(deny, SECURITY_DESCRIPTOR_REVISION);
    if (!NT_SUCCESS(status))
      return status;
    status = RtlCreateAcl(reinterpret_cast<PACL>(deny_acl_storage),
                          sizeof(deny_acl_storage), ACL_REVISION);
    if (!NT_SUCCESS(status))
      return status;
    return RtlSetDaclSecurityDescriptor(
        deny, TRUE, reinterpret_cast<PACL>(deny_acl_storage), FALSE);
  }

  PSECURITY_DESCRIPTOR allow() noexcept {
    return reinterpret_cast<PSECURITY_DESCRIPTOR>(allow_storage);
  }

  PSECURITY_DESCRIPTOR deny() noexcept {
    return reinterpret_cast<PSECURITY_DESCRIPTOR>(deny_storage);
  }

  PSECURITY_DESCRIPTOR administrators() noexcept {
    return reinterpret_cast<PSECURITY_DESCRIPTOR>(administrators_storage);
  }

private:
  alignas(PVOID) UCHAR allow_storage[64]{};
  alignas(PVOID) UCHAR deny_storage[64]{};
  alignas(PVOID) UCHAR administrators_storage[64]{};
  alignas(PVOID) UCHAR administrators_sid_storage[SECURITY_MAX_SID_SIZE]{};
  alignas(PVOID) UCHAR administrators_acl_storage[256]{};
  alignas(PVOID) UCHAR deny_acl_storage[256]{};
};

ntl::status add_security_test_ports_impl(
    ntl::flt::driver &driver,
    port_security_descriptors &descriptors) {
  ntl::flt::communication_server allow_server;
  ntl::flt::communication_port_options allow_options;
  allow_options.security_descriptor(descriptors.allow());
  auto status = driver.add_communication_port(security_allow_port_name,
                                               std::move(allow_server),
                                               allow_options);
  if (status.is_err())
    return status;

  ntl::flt::communication_server deny_server;
  ntl::flt::communication_port_options deny_options;
  deny_options.security_descriptor(descriptors.deny());
  status = driver.add_communication_port(security_deny_port_name,
                                         std::move(deny_server), deny_options);
  if (status.is_err())
    return status;

  ntl::flt::communication_server administrators_server;
  ntl::flt::communication_port_options administrators_options;
  administrators_options.security_descriptor(descriptors.administrators());
  return driver.add_communication_port(security_administrators_port_name,
                                        std::move(administrators_server),
                                        administrators_options);
}

struct connection_test_state {
  explicit connection_test_state(std::uint64_t value) noexcept
      : connection_id(value) {}

  std::uint64_t connection_id = 0;
  std::atomic<std::uint32_t> calls{0};
};

class test_notification_store final
    : public ntl::flt::communication_notification_store {
public:
  ntl::status
  persist(const ntl::flt::communication_record_view &) noexcept override {
    persisted_.fetch_add(1, std::memory_order_relaxed);
    return ntl::status::ok();
  }

  ntl::status acknowledge(const ntl::rpc::session_token &, std::uint32_t,
                          std::uint64_t) noexcept override {
    acknowledged_.fetch_add(1, std::memory_order_relaxed);
    return ntl::status::ok();
  }

  ntl::status acknowledge_batch(const ntl::rpc::session_token &, std::uint32_t,
                                const std::uint64_t *sequences,
                                std::size_t count) noexcept override {
    if (!sequences || count == 0)
      return ntl::status{STATUS_INVALID_PARAMETER};
    acknowledged_.fetch_add(static_cast<std::uint32_t>(count),
                            std::memory_order_relaxed);
    return ntl::status::ok();
  }

  ntl::status
  restore(const ntl::rpc::session_token &token,
          ntl::flt::communication_restore_sink &sink) noexcept override {
    if (token != restored_session_token)
      return ntl::status{STATUS_NOT_FOUND};

    try {
      progress_event event{0x52455354};
      std::vector<unsigned char> payload(
          decltype(progress_notification)::response_size());
      zpp::serializer::memory_view_output_archive archive(payload.data(),
                                                          payload.size());
      archive(event);
      payload.resize(archive.offset());
      const auto result =
          sink.add(decltype(progress_notification)::id(), 1,
                   ntl::flt::communication_record_kind::notification,
                   payload.data(), payload.size());
      if (result.is_ok())
        restored_.fetch_add(1, std::memory_order_relaxed);
      return result;
    } catch (const std::bad_alloc &) {
      return ntl::status{STATUS_INSUFFICIENT_RESOURCES};
    } catch (...) {
      return ntl::status{STATUS_UNSUCCESSFUL};
    }
  }

  std::uint64_t stats() const noexcept {
    return (static_cast<std::uint64_t>(
                persisted_.load(std::memory_order_relaxed))
            << 42) |
           (static_cast<std::uint64_t>(
                acknowledged_.load(std::memory_order_relaxed))
            << 21) |
           restored_.load(std::memory_order_relaxed);
  }

private:
  std::atomic<std::uint32_t> persisted_{0};
  std::atomic<std::uint32_t> acknowledged_{0};
  std::atomic<std::uint32_t> restored_{0};
};

std::atomic<std::uint32_t> connected_count{0};
std::atomic<std::uint32_t> disconnected_count{0};

} // namespace

ntl::status test_registry_notification_store() {
  if (KeGetCurrentIrql() != PASSIVE_LEVEL)
    return ntl::status{STATUS_INVALID_DEVICE_STATE};

  delete_notification_store_key();
  auto key = ntl::registry_key::create(
      notification_store_key, KEY_QUERY_VALUE | KEY_SET_VALUE,
      REG_OPTION_VOLATILE);
  if (!key)
    return key.status();

  ntl::status result = ntl::status::ok();
  {
    ntl::flt::registry_notification_store store(std::move(*key), 4096);
    const ntl::rpc::session_token token{0x1122334455667788ull,
                                        0x99aabbccddeeff00ull};
    const unsigned char first[]{1, 2, 3};
    const unsigned char second[]{4, 5};
    const unsigned char stream[]{6, 7, 8, 9};

    const ntl::flt::communication_record_view records[] = {
        {token, 1, 0x710, 10,
         ntl::flt::communication_record_kind::notification, first,
         sizeof(first)},
        {token, 1, 0x710, 11,
         ntl::flt::communication_record_kind::notification, second,
         sizeof(second)},
        {token, 1, 0x711, 20,
         ntl::flt::communication_record_kind::stream, stream, sizeof(stream)},
    };

    for (const auto &record : records) {
      result = store.persist(record);
      if (!result.is_ok())
        break;
    }

    test_restore_sink restored;
    if (result.is_ok()) {
      result = store.restore(token, restored);
      if (result.is_ok() &&
          (restored.records.size() != 3 ||
           restored.records[0].sequence != 10 ||
           restored.records[1].sequence != 11 ||
           restored.records[2].sequence != 20 ||
           restored.records[2].kind !=
               ntl::flt::communication_record_kind::stream))
        result = ntl::status{STATUS_DATA_ERROR};
    }

    const std::uint64_t acknowledged[]{10, 11};
    if (result.is_ok())
      result = store.acknowledge_batch(token, 0x710, acknowledged,
                                       RTL_NUMBER_OF(acknowledged));

    test_restore_sink remaining;
    if (result.is_ok()) {
      result = store.restore(token, remaining);
      if (result.is_ok() &&
          (remaining.records.size() != 1 ||
           remaining.records[0].sequence != 20))
        result = ntl::status{STATUS_DATA_ERROR};
    }

    if (result.is_ok())
      result = store.acknowledge(token, 0x711, 20);
    if (result.is_ok()) {
      test_restore_sink empty;
      const auto status = store.restore(token, empty);
      if (static_cast<NTSTATUS>(status) != STATUS_NOT_FOUND)
        result = ntl::status{STATUS_DATA_ERROR};
    }
  }

  delete_notification_store_key();
  return result;
}

ntl::status add_security_test_ports(ntl::flt::driver &driver) {
  static port_security_descriptors descriptors;
  const auto status = descriptors.initialize();
  if (!NT_SUCCESS(status))
    return status;
  return add_security_test_ports_impl(driver, descriptors);
}

void configure_advanced_communication_tests(
    ntl::flt::communication_server &server,
    const ntl::flt::communication_publisher &publisher) {
  auto store = std::make_shared<test_notification_store>();
  server
      .on_connect([](ntl::flt::communication_connection &connection) {
        connection.state(
            std::make_shared<connection_test_state>(connection.id()));
        connected_count.fetch_add(1, std::memory_order_relaxed);
        return ntl::status::ok();
      })
      .on_disconnect([](ntl::flt::communication_connection &connection) {
        if (connection.state<connection_test_state>())
          disconnected_count.fetch_add(1, std::memory_order_relaxed);
      })
      .notification_storage(store)
      .register_client_method(client_transform_method)
      .on(request_client_method,
          [publisher](const ntl::flt::communication_context &context,
                      std::uint32_t value) -> std::uint32_t {
            auto response = publisher.try_request(
                context.connection(), client_transform_method,
                std::chrono::seconds(2), value);
            if (!response)
              throw ntl::exception(response.status(),
                                   "User-mode request failed");
            return *response;
          })
      .on(publish_targeted_method,
          [publisher](const ntl::flt::communication_context &context,
                      std::uint32_t value) -> std::uint64_t {
            const auto status = publisher.try_notify(context.connection(),
                                                     progress_notification,
                                                     progress_event{value});
            if (status.is_err())
              throw ntl::exception(status, "Targeted notification failed");
            return context.connection().id();
          })
      .on(connection_state_method,
          [](const ntl::flt::communication_context &context) -> std::uint64_t {
            auto state = context.connection().state<connection_test_state>();
            if (!state)
              throw ntl::exception(STATUS_INVALID_DEVICE_STATE,
                                   "Connection state is unavailable");
            const auto calls =
                state->calls.fetch_add(1, std::memory_order_relaxed) + 1;
            return (state->connection_id << 32) | calls;
          })
      .on(storage_stats_method, [store]() noexcept { return store->stats(); })
      .on(connection_lifecycle_stats_method, []() noexcept {
        return (static_cast<std::uint64_t>(
                    connected_count.load(std::memory_order_relaxed))
                << 32) |
               disconnected_count.load(std::memory_order_relaxed);
      });
}

ntl::status add_drop_policy_test_port(ntl::flt::driver &driver) {
  ntl::flt::communication_server server;
  const auto publisher = server.publisher();
  server.register_notification(background_progress_notification)
      .on(drop_publish_method,
          [publisher](std::uint64_t session_id,
                      std::uint32_t value) -> std::uint64_t {
            auto sequence = publisher.try_notify(
                session_id, background_progress_notification,
                progress_event{value});
            if (!sequence)
              throw ntl::exception(sequence.status(),
                                   "Drop-policy notification failed");
            return *sequence;
          });

  ntl::flt::communication_port_options options;
  options.max_reliable_records(2).reliable_overflow(
      ntl::flt::communication_overflow_policy::drop_oldest);
  return driver.add_communication_port(drop_port_name, std::move(server),
                                       options);
}

ntl::status add_reject_test_port(ntl::flt::driver &driver) {
  ntl::flt::communication_server server;
  server.on_connect([](ntl::flt::communication_connection &) noexcept {
    return ntl::status{STATUS_ACCESS_DENIED};
  });
  ntl::flt::communication_port_options options;
  options.max_connections(2);
  return driver.add_communication_port(reject_port_name, std::move(server),
                                       options);
}

ntl::status add_byte_quota_test_port(ntl::flt::driver &driver) {
  ntl::flt::communication_server server;
  const auto publisher = server.publisher();
  server.register_notification(background_progress_notification)
      .on(drop_publish_method,
          [publisher](std::uint64_t session_id,
                      std::uint32_t value) -> std::uint64_t {
            auto sequence = publisher.try_notify(
                session_id, background_progress_notification,
                progress_event{value});
            if (!sequence)
              throw ntl::exception(sequence.status(),
                                   "Byte-quota notification failed");
            return *sequence;
          });

  ntl::flt::communication_port_options options;
  options.max_reliable_records(8).max_reliable_bytes(8);
  return driver.add_communication_port(byte_quota_port_name, std::move(server),
                                       options);
}

ntl::status add_connection_limit_test_port(ntl::flt::driver &driver) {
  ntl::flt::communication_server server;
  ntl::flt::communication_port_options options;
  options.max_connections(1);
  return driver.add_communication_port(connection_limit_port_name,
                                       std::move(server), options);
}

ntl::status add_session_limit_test_port(ntl::flt::driver &driver) {
  ntl::flt::communication_server server;
  ntl::flt::communication_port_options options;
  options.max_connections(2).max_sessions(1);
  return driver.add_communication_port(session_limit_port_name,
                                       std::move(server), options);
}

} // namespace crtsys_flt_runtime_test
