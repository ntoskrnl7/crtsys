#include <fltKernel.h>

#include <ntl/flt/all>
#include <ntl/ipc/shared_ring>
#include <ntl/irql>

#include "../shared/runtime_test.hpp"
#include "communication_advanced.hpp"

#include <atomic>
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>

namespace {
std::atomic<std::uint32_t> create_count{0};
std::atomic<std::uint32_t> successful_create_count{0};
std::atomic<std::uint32_t> read_count{0};
std::atomic<std::uint32_t> completed_read_count{0};
std::atomic<std::uint32_t> write_count{0};
std::atomic<std::uint32_t> written_bytes{0};
std::atomic<std::uint32_t> cleanup_count{0};
std::atomic<std::uint32_t> close_count{0};
std::atomic<std::uint32_t> file_context_writes{0};
std::atomic<std::uint32_t> stream_context_writes{0};
std::atomic<std::uint32_t> stream_handle_context_writes{0};
std::atomic<std::uint32_t> cached_file_name_uses{0};
std::atomic<std::uint32_t> cached_stream_name_uses{0};
std::atomic<std::uint32_t> completion_states_created{0};
std::atomic<std::uint32_t> completion_states_observed{0};
std::atomic<std::uint32_t> completion_states_destroyed{0};

struct io_completion_state {
  static constexpr std::uint32_t expected_marker = 0x43525453;

  io_completion_state() noexcept {
    completion_states_created.fetch_add(1, std::memory_order_relaxed);
  }

  ~io_completion_state() noexcept {
    completion_states_destroyed.fetch_add(1, std::memory_order_relaxed);
  }

  std::uint32_t marker = expected_marker;
};

struct instance_state {
  std::wstring name;
  std::wstring altitude;

  explicit instance_state(ntl::flt::instance_information &&information) noexcept
      : name(std::move(information.name)),
        altitude(std::move(information.altitude)) {}
  ~instance_state() noexcept = default;
};

struct file_state {
  ntl::flt::name_information cached_name;
  std::atomic<std::uint32_t> writes{0};

  explicit file_state(ntl::flt::name_information &&name) noexcept
      : cached_name(std::move(name)) {}
  ~file_state() noexcept {
    file_context_writes.fetch_add(writes.load(std::memory_order_relaxed),
                                  std::memory_order_relaxed);
  }
};

struct stream_state {
  ntl::flt::name_information cached_name;
  std::atomic<std::uint32_t> writes{0};

  explicit stream_state(ntl::flt::name_information &&name) noexcept
      : cached_name(std::move(name)) {}
  ~stream_state() noexcept {
    stream_context_writes.fetch_add(writes.load(std::memory_order_relaxed),
                                    std::memory_order_relaxed);
  }
};

struct stream_handle_state {
  std::atomic<std::uint32_t> writes{0};

  stream_handle_state() noexcept = default;
  ~stream_handle_state() noexcept {
    stream_handle_context_writes.fetch_add(
        writes.load(std::memory_order_relaxed), std::memory_order_relaxed);
  }
};

inline constexpr ntl::flt::file_context<file_state> file_state_context{};
inline constexpr ntl::flt::stream_context<stream_state> stream_state_context{};
inline constexpr ntl::flt::stream_handle_context<stream_handle_state>
    stream_handle_state_context{};
inline constexpr ntl::flt::instance_context<instance_state>
    instance_state_context{};

static_assert(std::atomic<std::uint32_t>::is_always_lock_free,
              "minifilter callbacks require a lock-free counter");

void observe_cached_names(ntl::flt::related_objects objects) noexcept {
  if (!ntl::is_irql_at_most(ntl::irql::apc))
    return;

  if (auto state = objects.try_get(file_state_context)) {
    if (!(*state)->cached_name.final_component().empty())
      cached_file_name_uses.fetch_add(1, std::memory_order_relaxed);
  }

  if (auto state = objects.try_get(stream_state_context)) {
    if (!(*state)->cached_name.name().empty())
      cached_stream_name_uses.fetch_add(1, std::memory_order_relaxed);
  }
}
} // namespace

ntl::status ntl::flt::main(ntl::flt::driver &driver, std::wstring_view) {
  using namespace crtsys_flt_runtime_test;

  const auto registry_store_status = test_registry_notification_store();
  if (registry_store_status.is_err())
    return registry_store_status;

  auto messages = make_server();
  const auto publisher = messages.publisher();
  configure_advanced_communication_tests(messages, publisher);
  messages.on(publish_progress_method,
              [publisher](const ntl::flt::communication_context &context,
                          std::uint32_t value, bool reliable) -> std::uint64_t {
                const progress_event event{value};
                if (!reliable) {
                  const auto status =
                      publisher.try_notify(progress_notification, event);
                  if (status.is_err())
                    throw ntl::exception(
                        status, "Transient progress notification failed");
                  return 0;
                }
                auto sequence = publisher.try_notify(
                    context.session_id(), progress_notification, event);
                if (!sequence)
                  throw ntl::exception(sequence.status(),
                                       "Reliable progress notification failed");
                return *sequence;
              });
  messages.on(publish_priority_method,
              [publisher](std::uint64_t session_id, std::uint32_t value,
                          bool critical) -> std::uint64_t {
                const progress_event event{value};
                const auto sequence =
                    critical
                        ? publisher.try_notify(
                              session_id, critical_progress_notification, event)
                        : publisher.try_notify(session_id,
                                               background_progress_notification,
                                               event);
                if (!sequence)
                  throw ntl::exception(sequence.status(),
                                       "Priority notification was not queued");
                return *sequence;
              });
  messages.on(
      shared_ring_method,
      [](const ntl::flt::communication_context &context,
         ntl::ipc::buffer_token app_to_driver,
         ntl::ipc::buffer_token driver_to_app) -> std::uint32_t {
        auto input = context.try_resolve(app_to_driver,
                                         ntl::ipc::region_access::driver_read);
        if (!input)
          throw ntl::exception(input.status(),
                               "Failed to resolve minifilter input ring");
        auto output = context.try_resolve(
            driver_to_app, ntl::ipc::region_access::driver_write);
        if (!output)
          throw ntl::exception(output.status(),
                               "Failed to resolve minifilter output ring");

        test_ring input_ring;
        test_ring output_ring;
        if (test_ring::attach(input->data(), input->size(), input_ring) !=
                ntl::ipc::validation_status::success ||
            test_ring::attach(output->data(), output->size(), output_ring) !=
                ntl::ipc::validation_status::success)
          throw ntl::exception(STATUS_INVALID_PARAMETER,
                               "Invalid minifilter shared-ring layout");

        std::uint32_t processed = 0;
        ring_record record{};
        while (input_ring.try_read(record)) {
          record.value += 1000;
          if (!output_ring.try_write(record))
            throw ntl::exception(STATUS_BUFFER_OVERFLOW,
                                 "Minifilter output ring is full");
          ++processed;
        }
        return processed;
      });

  ntl::ipc::region_limits region_limits;
  region_limits.max_regions(2).max_bytes(shared_region_bytes * 2);

  ntl::flt::communication_port_options port_options;
  port_options.max_pending_async(8)
      .max_reliable_records(4)
      .region_limits(region_limits);
  const auto port_status = driver.add_communication_port(
      port_name, std::move(messages), port_options);
  if (port_status.is_err())
    return port_status;
  const auto drop_port_status = add_drop_policy_test_port(driver);
  if (drop_port_status.is_err())
    return drop_port_status;
  const auto reject_port_status = add_reject_test_port(driver);
  if (reject_port_status.is_err())
    return reject_port_status;
  const auto byte_quota_status = add_byte_quota_test_port(driver);
  if (byte_quota_status.is_err())
    return byte_quota_status;
  const auto connection_limit_status = add_connection_limit_test_port(driver);
  if (connection_limit_status.is_err())
    return connection_limit_status;
  const auto session_limit_status = add_session_limit_test_port(driver);
  if (session_limit_status.is_err())
    return session_limit_status;
  const auto security_status = add_security_test_ports(driver);
  if (security_status.is_err())
    return security_status;

  ntl::flt::registration callbacks;
  callbacks
      .on_with_completion<io_completion_state>(
          ntl::flt::operation::create,
          [](ntl::flt::create_callback_data data, ntl::flt::related_objects,
             ntl::flt::completion_slot<io_completion_state>
                 &completion) noexcept {
            create_count.fetch_add(1, std::memory_order_relaxed);
            if (!ntl::is_irql_at_most(ntl::irql::apc))
              return ntl::flt::pre_result::success_no_callback;

            auto name = data.try_query_name(FLT_FILE_NAME_NORMALIZED |
                                            FLT_FILE_NAME_QUERY_DEFAULT);
            if (!name || name->try_parse().is_err() ||
                name->extension() != L"tmp")
              return ntl::flt::pre_result::success_no_callback;

            // The object moves into Filter Manager's CompletionContext only
            // when this callback requests a post operation. Otherwise the
            // slot destroys it before pre-operation returns.
            if (completion.try_emplace().is_err())
              return ntl::flt::pre_result::success_no_callback;
            return ntl::flt::pre_result::success_with_callback;
          },
          [](ntl::flt::create_callback_data data,
             ntl::flt::related_objects objects,
             ntl::flt::completion_ref<io_completion_state>
                 completion) noexcept {
            // This flags-less typed post callback is normal-completion-only.
            // NTL skips it while draining but still destroys completion.
            if (completion &&
                completion->marker == io_completion_state::expected_marker)
              completion_states_observed.fetch_add(1,
                                                   std::memory_order_relaxed);

            if (!data.io_status().is_ok())
              return;

            successful_create_count.fetch_add(1, std::memory_order_relaxed);
            auto name = data.try_query_name(FLT_FILE_NAME_NORMALIZED |
                                            FLT_FILE_NAME_QUERY_DEFAULT);
            if (!name || name->try_parse().is_err())
              return;

            auto cached_file_name = name->try_reference();
            if (cached_file_name) {
              (void)objects.try_get_or_create(file_state_context,
                                              std::move(*cached_file_name));
            }
            (void)objects.try_get_or_create(stream_state_context,
                                            std::move(*name));
            (void)objects.try_get_or_create(stream_handle_state_context);
          })
      .on_with_completion<io_completion_state>(
          ntl::flt::operation::read,
          [](ntl::flt::read_callback_data, ntl::flt::related_objects,
             ntl::flt::completion_slot<io_completion_state>
                 &completion) noexcept {
            read_count.fetch_add(1, std::memory_order_relaxed);
            if (completion.try_emplace().is_err())
              return ntl::flt::pre_result::success_no_callback;
            return ntl::flt::pre_result::success_with_callback;
          },
          [](ntl::flt::read_callback_data data, ntl::flt::related_objects,
             ntl::flt::completion_ref<io_completion_state>
                 completion) noexcept {
            if (completion &&
                completion->marker == io_completion_state::expected_marker)
              completion_states_observed.fetch_add(1,
                                                   std::memory_order_relaxed);

            if (!data.io_status().is_ok())
              return ntl::flt::post_continuation::finished;

            completed_read_count.fetch_add(1, std::memory_order_relaxed);
            // Force this test operation through the flags-less WhenSafe path.
            // Filter Manager can invoke the next callback immediately when the
            // current IRQL is already safe.
            return ntl::flt::post_continuation::when_safe;
          },
          [](ntl::flt::read_callback_data, ntl::flt::related_objects objects,
             ntl::flt::completion_ref<io_completion_state>
                 completion) noexcept {
            if (completion &&
                completion->marker == io_completion_state::expected_marker)
              observe_cached_names(objects);
          })
      .on(
          ntl::flt::operation::write,
          [](ntl::flt::write_callback_data data,
             ntl::flt::related_objects objects, void *&) noexcept {
            write_count.fetch_add(1, std::memory_order_relaxed);
            written_bytes.fetch_add(data.parameters().length(),
                                    std::memory_order_relaxed);

            if (ntl::is_irql_at_most(ntl::irql::apc)) {
              if (auto state = objects.try_get(file_state_context))
                (*state)->writes.fetch_add(1, std::memory_order_relaxed);
              if (auto state = objects.try_get(stream_state_context))
                (*state)->writes.fetch_add(1, std::memory_order_relaxed);
              if (auto state = objects.try_get(stream_handle_state_context))
                (*state)->writes.fetch_add(1, std::memory_order_relaxed);
            }
            observe_cached_names(objects);
            return ntl::flt::pre_result::success_no_callback;
          },
          ntl::flt::operation_flags::skip_paging_io)
      .on(ntl::flt::operation::cleanup,
          [](ntl::flt::callback_data<ntl::flt::operation::cleanup>,
             ntl::flt::related_objects objects, void *&) noexcept {
            cleanup_count.fetch_add(1, std::memory_order_relaxed);
            observe_cached_names(objects);
            return ntl::flt::pre_result::success_no_callback;
          })
      .on(ntl::flt::operation::close,
          [](ntl::flt::callback_data<ntl::flt::operation::close>,
             ntl::flt::related_objects objects, void *&) noexcept {
            close_count.fetch_add(1, std::memory_order_relaxed);
            observe_cached_names(objects);
            return ntl::flt::pre_result::success_no_callback;
          })
      .on_instance_setup([](ntl::flt::related_objects objects,
                            FLT_INSTANCE_SETUP_FLAGS flags, DEVICE_TYPE,
                            FLT_FILESYSTEM_TYPE) noexcept {
        auto information = objects.instance().try_information();
        if (!information)
          return information.status();

        DbgPrintEx(DPFLTR_IHVDRIVER_ID, DPFLTR_INFO_LEVEL,
                   "[crtsys FLT runtime] attach name=%ws altitude=%ws "
                   "volume=%ws mode=%s\n",
                   information->name.c_str(), information->altitude.c_str(),
                   information->volume_name.c_str(),
                   (flags & FLTFL_INSTANCE_SETUP_MANUAL_ATTACHMENT) ? "manual"
                                                                    : "auto");

        auto state = objects.try_get_or_create(instance_state_context,
                                               std::move(*information));
        return state ? ntl::status{STATUS_SUCCESS} : state.status();
      })
      .on_instance_teardown_start([](ntl::flt::related_objects objects,
                                     FLT_INSTANCE_TEARDOWN_FLAGS) noexcept {
        if (auto state = objects.try_get(instance_state_context)) {
          DbgPrintEx(DPFLTR_IHVDRIVER_ID, DPFLTR_INFO_LEVEL,
                     "[crtsys FLT runtime] detach name=%ws altitude=%ws\n",
                     (*state)->name.c_str(), (*state)->altitude.c_str());
        }
      })
      .on_unload([](ntl::flt::unload_flags) noexcept {
        DbgPrintEx(DPFLTR_IHVDRIVER_ID, DPFLTR_INFO_LEVEL,
                   "[crtsys FLT runtime] create=%lu successful=%lu read=%lu "
                   "completed=%lu write=%lu bytes=%lu cleanup=%lu close=%lu "
                   "contexts(file=%lu stream=%lu handle=%lu) "
                   "cached(file=%lu stream=%lu) "
                   "completion(created=%lu observed=%lu destroyed=%lu)\n",
                   create_count.load(std::memory_order_relaxed),
                   successful_create_count.load(std::memory_order_relaxed),
                   read_count.load(std::memory_order_relaxed),
                   completed_read_count.load(std::memory_order_relaxed),
                   write_count.load(std::memory_order_relaxed),
                   written_bytes.load(std::memory_order_relaxed),
                   cleanup_count.load(std::memory_order_relaxed),
                   close_count.load(std::memory_order_relaxed),
                   file_context_writes.load(std::memory_order_relaxed),
                   stream_context_writes.load(std::memory_order_relaxed),
                   stream_handle_context_writes.load(std::memory_order_relaxed),
                   cached_file_name_uses.load(std::memory_order_relaxed),
                   cached_stream_name_uses.load(std::memory_order_relaxed),
                   completion_states_created.load(std::memory_order_relaxed),
                   completion_states_observed.load(std::memory_order_relaxed),
                   completion_states_destroyed.load(std::memory_order_relaxed));
        return ntl::status{STATUS_SUCCESS};
      })
      .context(file_state_context)
      .context(stream_state_context)
      .context(stream_handle_state_context)
      .context(instance_state_context);

  return driver.start(std::move(callbacks));
}
