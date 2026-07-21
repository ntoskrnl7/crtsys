#include <fltKernel.h>

#include <ntl/flt/all>
#include <ntl/irql>

#include <atomic>
#include <cstdint>
#include <string_view>
#include <utility>

#include "communication.hpp"

namespace {

struct stream_state {
  ntl::flt::name_information name;
  std::atomic<std::uint32_t> writes{0};

  explicit stream_state(ntl::flt::name_information &&value) noexcept
      : name(std::move(value)) {}
  ~stream_state() noexcept = default;
};

inline constexpr ntl::flt::stream_context<stream_state> stream_state_context{};

void observe_stream(ntl::flt::related_objects objects) noexcept {
  if (!ntl::is_irql_at_most(ntl::irql::apc))
    return;

  if (auto state = objects.try_get(stream_state_context)) {
    // The name was captured during post-create. Later I/O paths can use this
    // stable snapshot without issuing another file-name query.
    (void)(*state)->name.name();
  }
}

} // namespace

ntl::status ntl::flt::main(ntl::flt::driver &driver, std::wstring_view) {
  const auto port_status =
      crtsys_minifilter_sample::add_communication_port(driver);
  if (port_status.is_err())
    return port_status;

  ntl::flt::registration callbacks;
  callbacks
      .on(
          ntl::flt::operation::create,
          [](ntl::flt::create_callback_data data,
             ntl::flt::related_objects) noexcept {
            if (!ntl::is_irql_at_most(ntl::irql::apc))
              return ntl::flt::pre_result::success_no_callback;

            auto name = data.try_query_name(FLT_FILE_NAME_NORMALIZED |
                                            FLT_FILE_NAME_QUERY_DEFAULT);
            if (!name || name->try_parse().is_err() ||
                name->extension() != L"tmp")
              return ntl::flt::pre_result::success_no_callback;

            return ntl::flt::pre_result::success_with_callback;
          },
          [](ntl::flt::create_callback_data data,
             ntl::flt::related_objects objects) noexcept {
            // A flags-less post callback is normal-completion-only. NTL does
            // not invoke it while Filter Manager drains this instance.
            if (!data.io_status().is_ok())
              return;

            auto name = data.try_query_name(FLT_FILE_NAME_NORMALIZED |
                                            FLT_FILE_NAME_QUERY_DEFAULT);
            if (name && name->try_parse().is_ok()) {
              (void)objects.try_get_or_create(stream_state_context,
                                              std::move(*name));
            }
          })
      .on(ntl::flt::operation::read,
          [](ntl::flt::read_callback_data,
             ntl::flt::related_objects objects) noexcept {
            observe_stream(objects);
            return ntl::flt::pre_result::success_no_callback;
          })
      .on(
          ntl::flt::operation::write,
          [](ntl::flt::write_callback_data,
             ntl::flt::related_objects objects) noexcept {
            if (ntl::is_irql_at_most(ntl::irql::apc)) {
              if (auto state = objects.try_get(stream_state_context))
                (*state)->writes.fetch_add(1, std::memory_order_relaxed);
            }
            observe_stream(objects);
            return ntl::flt::pre_result::success_no_callback;
          },
          ntl::flt::operation_flags::skip_paging_io)
      .on(ntl::flt::operation::cleanup,
          [](ntl::flt::cleanup_callback_data,
             ntl::flt::related_objects objects) noexcept {
            observe_stream(objects);
            return ntl::flt::pre_result::success_no_callback;
          })
      .context(stream_state_context);

  return driver.start(std::move(callbacks));
}
