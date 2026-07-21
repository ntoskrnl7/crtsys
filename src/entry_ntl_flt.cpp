// WDK 10.0.28000 requires ntifs.h to establish its process/thread aliases
// before ntddk.h is included by another NTL header.
// clang-format off
#include <fltKernel.h>
#include "../include/ntl/expand_stack"
#include "../include/ntl/flt/driver"
// clang-format on

#include "runtime_internal.h"

#include <functional>
#include <memory>
#include <string_view>

EXTERN_C DRIVER_INITIALIZE CrtSysNtlFltDriverEntry;

#ifdef ALLOC_PRAGMA
#pragma alloc_text(INIT, CrtSysNtlFltDriverEntry)
#endif

namespace {
std::unique_ptr<ntl::flt::driver> this_filter_driver;
PDRIVER_OBJECT this_driver_object = nullptr;
} // namespace

namespace ntl::flt {
class driver_initializer {
public:
  static std::unique_ptr<driver> make_driver(PDRIVER_OBJECT object) {
    return std::unique_ptr<driver>(new driver(object));
  }
};

namespace detail {
FLT_PREOP_CALLBACK_STATUS FLTAPI
pre_operation_trampoline(PFLT_CALLBACK_DATA data, PCFLT_RELATED_OBJECTS objects,
                         PVOID *completion_context) noexcept {
  if (!this_filter_driver || !data || !data->Iopb)
    return FLT_PREOP_SUCCESS_NO_CALLBACK;

  const auto callbacks = this_filter_driver->registration_
                             .callback_index_[data->Iopb->MajorFunction];
  if (!callbacks || !callbacks->pre)
    return FLT_PREOP_SUCCESS_NO_CALLBACK;

  void *typed_context = nullptr;
  const pre_result result = callbacks->pre.invoke(data, objects, typed_context);
  if (completion_context)
    *completion_context = typed_context;
  return static_cast<FLT_PREOP_CALLBACK_STATUS>(result);
}

FLT_POSTOP_CALLBACK_STATUS FLTAPI post_operation_trampoline(
    PFLT_CALLBACK_DATA data, PCFLT_RELATED_OBJECTS objects,
    PVOID completion_context, FLT_POST_OPERATION_FLAGS flags) noexcept {
  if (!this_filter_driver || !data || !data->Iopb)
    return FLT_POSTOP_FINISHED_PROCESSING;

  const UCHAR operation = data->Iopb->MajorFunction;
  const auto callbacks =
      this_filter_driver->registration_.callback_index_[operation];
  if (!callbacks || !callbacks->post)
    return FLT_POSTOP_FINISHED_PROCESSING;

  const bool draining = (flags & FLTFL_POST_OPERATION_DRAINING) != 0;
  if (draining && !callbacks->post.invokes_during_draining()) {
    // A flags-less post callback declares normal-completion-only work. Typed
    // completion state still has to be destroyed while the instance drains.
    callbacks->post.cleanup(completion_context);
    return FLT_POSTOP_FINISHED_PROCESSING;
  }

  if (callbacks->post.has_safe_continuation()) {
    const auto continuation = callbacks->post.invoke_immediate(
        data, objects, completion_context, flags);
    if (continuation == post_continuation::finished) {
      callbacks->post.cleanup(completion_context);
      return FLT_POSTOP_FINISHED_PROCESSING;
    }

    if (draining || !FLT_IS_IRP_OPERATION(data)) {
      // No safe callback will receive the context on this path.
      callbacks->post.cleanup(completion_context);
      return FLT_POSTOP_FINISHED_PROCESSING;
    }

    FLT_POSTOP_CALLBACK_STATUS result = FLT_POSTOP_FINISHED_PROCESSING;
    const BOOLEAN scheduled = FltDoCompletionProcessingWhenSafe(
        data, objects, completion_context, flags,
        safe_post_operation_trampoline, &result);
    if (!scheduled) {
      // Filter Manager neither invoked nor queued the safe callback.
      callbacks->post.cleanup(completion_context);
    }
    return result;
  }

  return static_cast<FLT_POSTOP_CALLBACK_STATUS>(
      callbacks->post.invoke(data, objects, completion_context, flags));
}

FLT_POSTOP_CALLBACK_STATUS FLTAPI safe_post_operation_trampoline(
    PFLT_CALLBACK_DATA data, PCFLT_RELATED_OBJECTS objects,
    PVOID completion_context, FLT_POST_OPERATION_FLAGS) noexcept {
  if (!this_filter_driver || !data || !data->Iopb)
    return FLT_POSTOP_FINISHED_PROCESSING;

  const auto callbacks = this_filter_driver->registration_
                             .callback_index_[data->Iopb->MajorFunction];
  if (callbacks && callbacks->post.has_safe_continuation())
    callbacks->post.invoke_safe(data, objects, completion_context);
  return FLT_POSTOP_FINISHED_PROCESSING;
}

NTSTATUS FLTAPI
filter_unload_trampoline(FLT_FILTER_UNLOAD_FLAGS flags) noexcept {
  if (!this_filter_driver)
    return STATUS_SUCCESS;

  const auto callback = this_filter_driver->registration_.unload_callback_;
  NTSTATUS result = STATUS_SUCCESS;
  if (callback)
    result = callback(unload_flags{flags});

  if (!NT_SUCCESS(result) && (flags & FLTFL_FILTER_UNLOAD_MANDATORY) == 0) {
    return result;
  }

  this_filter_driver->unregister_filter();
  this_filter_driver.reset();
  CrtSysUninitializeRuntime(this_driver_object);
  this_driver_object = nullptr;
  return STATUS_SUCCESS;
}

NTSTATUS FLTAPI instance_setup_trampoline(
    PCFLT_RELATED_OBJECTS objects, FLT_INSTANCE_SETUP_FLAGS flags,
    DEVICE_TYPE volume_device_type,
    FLT_FILESYSTEM_TYPE filesystem_type) noexcept {
  if (!this_filter_driver)
    return STATUS_FLT_DO_NOT_ATTACH;
  const auto callback =
      this_filter_driver->registration_.instance_setup_callback_;
  if (!callback)
    return STATUS_SUCCESS;
  return static_cast<NTSTATUS>(callback(related_objects{objects}, flags,
                                        volume_device_type, filesystem_type));
}

NTSTATUS FLTAPI instance_query_teardown_trampoline(
    PCFLT_RELATED_OBJECTS objects,
    FLT_INSTANCE_QUERY_TEARDOWN_FLAGS flags) noexcept {
  if (!this_filter_driver)
    return STATUS_FLT_DO_NOT_DETACH;
  const auto callback =
      this_filter_driver->registration_.instance_query_teardown_callback_;
  if (!callback)
    return STATUS_FLT_DO_NOT_DETACH;
  return static_cast<NTSTATUS>(callback(related_objects{objects}, flags));
}

void FLTAPI instance_teardown_start_trampoline(
    PCFLT_RELATED_OBJECTS objects, FLT_INSTANCE_TEARDOWN_FLAGS flags) noexcept {
  if (!this_filter_driver)
    return;
  const auto callback =
      this_filter_driver->registration_.instance_teardown_start_callback_;
  if (callback)
    callback(related_objects{objects}, flags);
}

void FLTAPI instance_teardown_complete_trampoline(
    PCFLT_RELATED_OBJECTS objects, FLT_INSTANCE_TEARDOWN_FLAGS flags) noexcept {
  if (!this_filter_driver)
    return;
  const auto callback =
      this_filter_driver->registration_.instance_teardown_complete_callback_;
  if (callback)
    callback(related_objects{objects}, flags);
}
} // namespace detail
} // namespace ntl::flt

EXTERN_C
NTSTATUS
CrtSysNtlFltDriverEntry(PDRIVER_OBJECT driver_object,
                        PUNICODE_STRING registry_path) {
  PAGED_CODE();

  NTSTATUS result = CrtSysInitializeRuntime(driver_object, registry_path);
  if (!NT_SUCCESS(result))
    return result;

  this_driver_object = driver_object;
  try {
    this_filter_driver =
        ntl::flt::driver_initializer::make_driver(driver_object);
    const std::wstring_view path{
        registry_path->Buffer,
        static_cast<std::size_t>(registry_path->Length / sizeof(wchar_t))};
    const ntl::status entry_status =
        ntl::expand_stack(ntl::flt::main, std::ref(*this_filter_driver), path);
    if (entry_status.is_err() || !this_filter_driver->started()) {
      this_filter_driver.reset();
      this_driver_object = nullptr;
      CrtSysUninitializeRuntime(driver_object);
      return entry_status.is_err() ? static_cast<NTSTATUS>(entry_status)
                                   : STATUS_INVALID_DEVICE_STATE;
    }
  } catch (const ntl::exception &error) {
    this_filter_driver.reset();
    this_driver_object = nullptr;
    CrtSysUninitializeRuntime(driver_object);
    return error.get_status();
  } catch (...) {
    this_filter_driver.reset();
    this_driver_object = nullptr;
    CrtSysUninitializeRuntime(driver_object);
    return STATUS_UNSUCCESSFUL;
  }

  return STATUS_SUCCESS;
}
