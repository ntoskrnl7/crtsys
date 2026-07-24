#include <fltKernel.h>

#include <ntl/flt/all>
#include <ntl/irql>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <string_view>

#include "swap_buffers_sample.hpp"
#include "xor_filter.hpp"

namespace crtsys_minifilter_swap_buffers_sample {
namespace {

bool equal_ascii_case_insensitive(std::wstring_view left,
                                  std::wstring_view right) noexcept {
  if (left.size() != right.size())
    return false;
  for (std::size_t index = 0; index != left.size(); ++index) {
    auto a = left[index];
    auto b = right[index];
    if (a >= L'A' && a <= L'Z')
      a = static_cast<wchar_t>(a - L'A' + L'a');
    if (b >= L'A' && b <= L'Z')
      b = static_cast<wchar_t>(b - L'A' + L'a');
    if (a != b)
      return false;
  }
  return true;
}

enum class file_selection { unprotected, protected_file, unknown };

template <class Data>
file_selection select_file(Data data) noexcept {
  if (!ntl::is_irql_at_most(ntl::irql::apc))
    return file_selection::unknown;
  auto name = data.try_query_name(FLT_FILE_NAME_NORMALIZED |
                                  FLT_FILE_NAME_QUERY_DEFAULT);
  if (!name || name->try_parse().is_err())
    return file_selection::unknown;
  return equal_ascii_case_insensitive(name->extension(), protected_extension)
             ? file_selection::protected_file
             : file_selection::unprotected;
}

void xor_bytes(void *buffer, std::size_t length) noexcept {
  auto *bytes = static_cast<std::uint8_t *>(buffer);
  for (std::size_t index = 0; index != length; ++index)
    bytes[index] ^= xor_key;
}

template <class Data>
ntl::flt::pre_result fail_pre(Data data, ntl::status status) noexcept {
  data.complete(status, 0);
  return ntl::flt::pre_result::complete;
}

ntl::flt::pre_result
prepare_xor_write_at_passive(ntl::flt::write_callback_data data,
                             void *&completion_context) noexcept {
  completion_context = nullptr;
  const auto selection = select_file(data);
  if (selection == file_selection::unprotected)
    return ntl::flt::pre_result::success_no_callback;
  if (selection == file_selection::unknown)
    return fail_pre(data, STATUS_OBJECT_NAME_INVALID);

  auto swapped =
      ntl::flt::try_swap_io_buffers(ntl::flt::as_pre(data));
  if (!swapped)
    return fail_pre(data, swapped.status());
  if (!swapped->input() || !swapped->input()->data()) {
    (void)swapped->complete();
    return fail_pre(data, STATUS_INVALID_DEVICE_STATE);
  }

  xor_bytes(swapped->input()->data(), swapped->input()->size());
  const auto applied = swapped->apply();
  if (!applied.is_ok()) {
    (void)swapped->complete();
    return fail_pre(data, applied);
  }

  auto released = swapped->release_completion_context();
  if (!released) {
    (void)swapped->complete();
    return fail_pre(data, released.status());
  }
  completion_context = *released;
  return ntl::flt::pre_result::success_with_callback;
}

ntl::flt::pre_result
prepare_xor_read_at_passive(ntl::flt::read_callback_data data,
                            void *&completion_context) noexcept {
  completion_context = nullptr;
  const auto selection = select_file(data);
  if (selection == file_selection::unprotected)
    return ntl::flt::pre_result::success_no_callback;
  if (selection == file_selection::unknown)
    return fail_pre(data, STATUS_OBJECT_NAME_INVALID);

  auto swapped =
      ntl::flt::try_swap_io_buffers(ntl::flt::as_pre(data));
  if (!swapped)
    return fail_pre(data, swapped.status());
  if (!swapped->output() || !swapped->output()->data()) {
    (void)swapped->complete();
    return fail_pre(data, STATUS_INVALID_DEVICE_STATE);
  }

  const auto applied = swapped->apply();
  if (!applied.is_ok()) {
    (void)swapped->complete();
    return fail_pre(data, applied);
  }

  auto released = swapped->release_completion_context();
  if (!released) {
    (void)swapped->complete();
    return fail_pre(data, released.status());
  }
  completion_context = *released;
  return ntl::flt::pre_result::success_with_callback;
}

ntl::status
deferred_write_pre(PFLT_CALLBACK_DATA native, void *,
                   ntl::flt::deferred_pre_completion &completion) noexcept {
  completion = {};
  completion.result = prepare_xor_write_at_passive(
      ntl::flt::write_callback_data{native}, completion.completion_context);
  return STATUS_SUCCESS;
}

ntl::status
deferred_read_pre(PFLT_CALLBACK_DATA native, void *,
                  ntl::flt::deferred_pre_completion &completion) noexcept {
  completion = {};
  completion.result = prepare_xor_read_at_passive(
      ntl::flt::read_callback_data{native}, completion.completion_context);
  return STATUS_SUCCESS;
}

template <class Data>
ntl::flt::pre_result queue_xor_pre(Data data, void *&completion_context,
                                   ntl::flt::deferred_pre_routine routine,
                                   auto prepare) noexcept {
  completion_context = nullptr;
  if (data.parameters().length() == 0)
    return ntl::flt::pre_result::success_no_callback;

  if (data.is_fast_io_operation()) {
    if (select_file(data) == file_selection::unprotected)
      return ntl::flt::pre_result::success_no_callback;
    return ntl::flt::pre_result::disallow_fast_io;
  }

  if (select_file(data) == file_selection::unprotected)
    return ntl::flt::pre_result::success_no_callback;

  if (ntl::is_passive_level())
    return prepare(data, completion_context);

  const auto queued =
      ntl::flt::queue_pre_operation_at_passive(ntl::flt::as_pre(data), routine);
  if (!queued.is_ok())
    return fail_pre(data, queued);
  return ntl::flt::pre_result::pending;
}

ntl::status finish_swap(void *completion_context) noexcept {
  auto swapped = ntl::flt::swapped_io_buffers::adopt_completion_context(
      completion_context);
  if (!swapped)
    return swapped.status();
  return swapped->complete();
}

ntl::status
finish_xor_read_at_passive(ntl::flt::read_callback_data data,
                           void *completion_context) noexcept {
  auto swapped = ntl::flt::swapped_io_buffers::adopt_completion_context(
      completion_context);
  if (!swapped)
    return swapped.status();

  if (data.io_status().is_err()) {
    (void)swapped->complete();
    return ntl::status::ok();
  }

  const auto valid_bytes =
      (std::min)(swapped->output() ? swapped->output()->size() : std::size_t{0},
                 static_cast<std::size_t>(data.information()));
  if (!swapped->output() || !swapped->output()->data()) {
    (void)swapped->complete();
    return STATUS_INVALID_DEVICE_STATE;
  }

  const auto prepared = swapped->prepare_output_copy_back(valid_bytes);
  if (!prepared.is_ok()) {
    (void)swapped->complete();
    return prepared;
  }

  xor_bytes(swapped->output()->data(), valid_bytes);
  const auto copied = swapped->copy_back(ntl::flt::as_post(data));
  const auto completed = swapped->complete();
  if (!copied.is_ok())
    return copied;
  return completed;
}

ntl::status deferred_read_post(PFLT_CALLBACK_DATA native,
                               void *completion_context) noexcept {
  return finish_xor_read_at_passive(ntl::flt::read_callback_data{native},
                                    completion_context);
}

ntl::flt::pre_result write_pre(ntl::flt::write_callback_data data,
                               ntl::flt::related_objects,
                               void *&completion_context) noexcept {
  return queue_xor_pre(data, completion_context, &deferred_write_pre,
                       &prepare_xor_write_at_passive);
}

ntl::flt::post_result
write_post(ntl::flt::write_callback_data data, ntl::flt::related_objects,
           void *completion_context,
           ntl::flt::post_operation_flags flags) noexcept {
  const auto status = finish_swap(completion_context);
  if (!flags.draining() && !status.is_ok())
    data.set_io_status(status);
  return ntl::flt::post_result::finished;
}

ntl::flt::pre_result read_pre(ntl::flt::read_callback_data data,
                              ntl::flt::related_objects,
                              void *&completion_context) noexcept {
  return queue_xor_pre(data, completion_context, &deferred_read_pre,
                       &prepare_xor_read_at_passive);
}

ntl::flt::post_result
read_post(ntl::flt::read_callback_data data, ntl::flt::related_objects,
          void *completion_context,
          ntl::flt::post_operation_flags flags) noexcept {
  if (flags.draining() || data.io_status().is_err()) {
    (void)finish_swap(completion_context);
    return ntl::flt::post_result::finished;
  }

  if (ntl::is_passive_level()) {
    const auto status = finish_xor_read_at_passive(data, completion_context);
    if (!status.is_ok())
      data.set_io_status(status);
    return ntl::flt::post_result::finished;
  }

  const auto queued = ntl::flt::queue_post_operation_at_passive(
      ntl::flt::as_post(data), &deferred_read_post, completion_context);
  if (queued.is_ok())
    return ntl::flt::post_result::more_processing_required;

  (void)finish_swap(completion_context);
  data.set_io_status(queued);
  return ntl::flt::post_result::finished;
}

} // namespace

void add_xor_callbacks(ntl::flt::registration &callbacks) {
  callbacks
      .on(ntl::flt::operation::write, &write_pre, &write_post,
          ntl::flt::operation_flags::skip_paging_io)
      .on(ntl::flt::operation::read, &read_pre, &read_post,
          ntl::flt::operation_flags::skip_paging_io);
}

} // namespace crtsys_minifilter_swap_buffers_sample
