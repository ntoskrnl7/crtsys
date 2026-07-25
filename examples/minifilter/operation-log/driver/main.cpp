#include <ntl/flt/all>
#include <ntl/irql>

#include "operation_log_sample.hpp"
#include "record_queue.hpp"

#include <cstdint>
#include <string_view>
#include <utility>

namespace {

using crtsys_minifilter_operation_log_sample::operation_records;
using crtsys_minifilter_operation_log_sample::phase;
using sample_operation =
    crtsys_minifilter_operation_log_sample::operation;

struct tracked_handle {
};

struct completion_state {
  sample_operation operation_id;
};

inline constexpr ntl::flt::stream_handle_context<tracked_handle>
    tracked_handle_context{};

bool is_tracked(ntl::flt::related_objects objects) noexcept {
  return static_cast<bool>(objects.try_get(tracked_handle_context));
}

ntl::flt::pre_result
begin_io(ntl::flt::related_objects objects,
         ntl::flt::completion_slot<completion_state> &completion,
         sample_operation operation_id, std::uint32_t requested) noexcept {
  if (!is_tracked(objects))
    return ntl::flt::pre_result::success_no_callback;

  operation_records.push(operation_id, phase::pre, STATUS_PENDING, requested);
  if (completion.try_emplace(operation_id).is_err())
    return ntl::flt::pre_result::success_no_callback;
  return ntl::flt::pre_result::success_with_callback;
}

template <class Data>
void finish_io(Data data,
               ntl::flt::completion_ref<completion_state> completion) noexcept {
  if (!completion)
    return;
  operation_records.push(
      completion->operation_id, phase::post,
      static_cast<NTSTATUS>(data.io_status()),
      static_cast<std::uint32_t>(data.information()));
}

} // namespace

ntl::status ntl::flt::main(ntl::flt::driver &driver, std::wstring_view) {
  using namespace crtsys_minifilter_operation_log_sample;

  operation_records.initialize();
  auto messages = make_server();
  messages
      .on(reset_method, []() noexcept -> std::uint32_t {
        operation_records.reset();
        return 1;
      })
      .on(read_method, []() noexcept { return operation_records.read(); });

  const ntl::status port =
      driver.add_communication_port(port_name, std::move(messages));
  if (port.is_err())
    return port;

  ntl::flt::registration callbacks;
  callbacks
      .context(tracked_handle_context)
      .on_with_completion<completion_state>(
          ntl::flt::operation::create,
          [](ntl::flt::create_callback_data data, ntl::flt::related_objects,
             ntl::flt::completion_slot<completion_state>
                 &completion) noexcept {
            if (!ntl::is_irql_at_most(ntl::irql::apc))
              return ntl::flt::pre_result::success_no_callback;

            auto name = data.try_query_name(FLT_FILE_NAME_NORMALIZED |
                                            FLT_FILE_NAME_QUERY_DEFAULT);
            if (!name || name->try_parse().is_err() ||
                name->extension() != L"ntlspy") {
              return ntl::flt::pre_result::success_no_callback;
            }

            operation_records.push(sample_operation::create, phase::pre,
                                   STATUS_PENDING);
            if (completion.try_emplace(sample_operation::create).is_err())
              return ntl::flt::pre_result::success_no_callback;
            return ntl::flt::pre_result::success_with_callback;
          },
          [](ntl::flt::create_callback_data data,
             ntl::flt::related_objects objects,
             ntl::flt::completion_ref<completion_state>
                 completion) noexcept {
            finish_io(data, completion);
            if (data.io_status().is_ok())
              (void)objects.try_get_or_create(tracked_handle_context);
          })
      .on_with_completion<completion_state>(
          ntl::flt::operation::read,
          [](ntl::flt::read_callback_data data,
             ntl::flt::related_objects objects,
             ntl::flt::completion_slot<completion_state>
                 &completion) noexcept {
            return begin_io(objects, completion, sample_operation::read,
                            data.parameters().length());
          },
          [](ntl::flt::read_callback_data data, ntl::flt::related_objects,
             ntl::flt::completion_ref<completion_state>
                 completion) noexcept { finish_io(data, completion); })
      .on_with_completion<completion_state>(
          ntl::flt::operation::write,
          [](ntl::flt::write_callback_data data,
             ntl::flt::related_objects objects,
             ntl::flt::completion_slot<completion_state>
                 &completion) noexcept {
            return begin_io(objects, completion, sample_operation::write,
                            data.parameters().length());
          },
          [](ntl::flt::write_callback_data data, ntl::flt::related_objects,
             ntl::flt::completion_ref<completion_state>
                 completion) noexcept { finish_io(data, completion); })
      .on(ntl::flt::operation::cleanup,
          [](ntl::flt::cleanup_callback_data,
             ntl::flt::related_objects objects, void *&) noexcept {
            if (is_tracked(objects))
              operation_records.push(sample_operation::cleanup, phase::pre,
                                     STATUS_PENDING);
            return ntl::flt::pre_result::success_no_callback;
          })
      .on(ntl::flt::operation::close,
          [](ntl::flt::close_callback_data,
             ntl::flt::related_objects objects, void *&) noexcept {
            if (is_tracked(objects))
              operation_records.push(sample_operation::close, phase::pre,
                                     STATUS_PENDING);
            return ntl::flt::pre_result::success_no_callback;
          })
      .on_unload([](ntl::flt::unload_flags) noexcept {
        operation_records.close();
        return ntl::status{STATUS_SUCCESS};
      });

  return driver.start(std::move(callbacks));
}
