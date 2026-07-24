/**
 * @file ntl_ipc_process_bridge.cpp
 * @brief NTIFS-backed process attachment used by NTL IPC mappings.
 */

#include <ntifs.h>

#include "../../../include/ntl/ipc/detail/process_bridge"

namespace ntl::ipc::detail {

namespace {
using acquire_process_exit_fn = NTSTATUS(NTAPI *)(PEPROCESS);
using release_process_exit_fn = void(NTAPI *)(PEPROCESS);

bool resolve_exit_synchronization(acquire_process_exit_fn &acquire,
                                  release_process_exit_fn &release) noexcept {
  UNICODE_STRING acquire_name =
      RTL_CONSTANT_STRING(L"PsAcquireProcessExitSynchronization");
  UNICODE_STRING release_name =
      RTL_CONSTANT_STRING(L"PsReleaseProcessExitSynchronization");
  acquire = reinterpret_cast<acquire_process_exit_fn>(
      MmGetSystemRoutineAddress(&acquire_name));
  release = reinterpret_cast<release_process_exit_fn>(
      MmGetSystemRoutineAddress(&release_name));
  return acquire && release;
}
} // namespace

NTSTATUS NTAPI invoke_in_process(void *opaque_process,
                                 attached_process_routine routine,
                                 void *context) noexcept {
  if (!opaque_process || !routine)
    return STATUS_INVALID_PARAMETER;
  if (KeGetCurrentIrql() != PASSIVE_LEVEL)
    return STATUS_INVALID_DEVICE_STATE;

  PEPROCESS process = static_cast<PEPROCESS>(opaque_process);

  acquire_process_exit_fn acquire = nullptr;
  release_process_exit_fn release = nullptr;
  if (!resolve_exit_synchronization(acquire, release))
    return STATUS_NOT_SUPPORTED;

  const NTSTATUS synchronized = acquire(process);
  if (!NT_SUCCESS(synchronized))
    return synchronized;

  NTSTATUS result = STATUS_SUCCESS;
  KAPC_STATE state{};
  const bool attach = PsGetCurrentProcess() != process;
  if (attach)
    KeStackAttachProcess(reinterpret_cast<PRKPROCESS>(process), &state);

  // Every bridge routine is noexcept and contains its own SEH boundary for
  // user-address access. Keeping SEH out of this frame also avoids pulling the
  // user-mode GS/SEH personality into kernel links.
  result = routine(context);
  if (attach)
    KeUnstackDetachProcess(&state);
  release(process);
  return result;
}

NTSTATUS NTAPI reference_requestor_process(PIRP irp, void **process) noexcept {
  if (!irp || !process)
    return STATUS_INVALID_PARAMETER;
  *process = nullptr;
  PEPROCESS requestor = IoGetRequestorProcess(irp);
  if (!requestor)
    return STATUS_INVALID_CID;
  ObReferenceObject(requestor);
  *process = requestor;
  return STATUS_SUCCESS;
}

HANDLE NTAPI query_process_id(void *opaque_process) noexcept {
  return opaque_process
             ? PsGetProcessId(static_cast<PEPROCESS>(opaque_process))
             : nullptr;
}

void *NTAPI query_system_range_start() noexcept {
  return MmSystemRangeStart;
}

} // namespace ntl::ipc::detail
