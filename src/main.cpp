// clang-format off

#include <Ldk/windows.h>

#include <corecrt_startup.h>
#include <vcruntime_internal.h>
#include <Ldk/ldk.h>

EXTERN_C DRIVER_INITIALIZE CrtSysDriverEntry;
EXTERN_C DRIVER_UNLOAD CrtSysDriverUnload;

#ifdef ALLOC_PRAGMA
#pragma alloc_text(INIT, CrtSysDriverEntry)
#pragma alloc_text(PAGE, CrtSysDriverUnload)
#endif

#if CRTSYS_USE_NTL_MAIN
// clang-format on
#include "../include/ntl/driver"
#include "../include/ntl/expand_stack"
#include <memory>
std::unique_ptr<ntl::driver> this_driver;

namespace ntl {
class device_dispatch_invoker {
public:
  static status invoke(ntl::device &device, PIRP irp) {
    auto ctx = device.get_context<void>();
    NTSTATUS Status = STATUS_INVALID_DEVICE_REQUEST;
    irp->IoStatus.Information = 0;
    PIO_STACK_LOCATION irp_sp = IoGetCurrentIrpStackLocation(irp);
    switch (irp_sp->MajorFunction) {
    case IRP_MJ_CREATE:
      if (ctx->on_device_control)
        Status = STATUS_SUCCESS;
      break;
    case IRP_MJ_CLOSE:
      if (ctx->on_device_control)
        Status = STATUS_SUCCESS;
      break;
    case IRP_MJ_DEVICE_CONTROL:
      if (ctx->on_device_control) {
        __try {
          irp->IoStatus.Information =
              irp_sp->Parameters.DeviceIoControl.OutputBufferLength;
          size_t out_len = (size_t)irp->IoStatus.Information;
          ctx->on_device_control(
              irp_sp->Parameters.DeviceIoControl.IoControlCode,
              (const uint8_t *)irp->AssociatedIrp.SystemBuffer,
              (size_t)irp_sp->Parameters.DeviceIoControl.InputBufferLength,
              (uint8_t *)irp->AssociatedIrp.SystemBuffer, &out_len);
          irp->IoStatus.Information = (ULONG_PTR)out_len;
          Status = STATUS_SUCCESS;
        } __except (EXCEPTION_EXECUTE_HANDLER) {
          Status = GetExceptionCode();
          irp->IoStatus.Information = 0;
        }
      }
      break;
    }
    irp->IoStatus.Status = Status;
    IoCompleteRequest(irp, IO_NO_INCREMENT);
    return Status;
  }
};
} // namespace ntl
  // clang-format off
#else
EXTERN_C DRIVER_INITIALIZE DriverEntry;
PDRIVER_UNLOAD CrtsyspDriverUnload = NULL;
PDRIVER_DISPATCH CrtsyspDispatchDeviceControl = NULL;
PDRIVER_DISPATCH CrtsyspDispatchCreate = NULL;
PDRIVER_DISPATCH CrtsyspDispatchClose = NULL;
#endif

//
// :-(
// TEB Tls(gs[0x58], fs[0x18])에 직접 접근하는 코드가 존재하므로 임시로 버퍼를 설정합니다.
// %ProgramFiles(x86)%\Microsoft Visual Studio\2019\Community\VC\Tools\MSVC\14.24.28314\include\pplwin.h Line:84
//
CHAR CrtSyspTlsBuffer[1024 * 1024];
PVOID CrtSyspTlsSlots[1024] = { &CrtSyspTlsBuffer, };

VOID
CrtSysSetTebThreadLocalStoragePointer (
    _In_ PVOID ThreadLocalStoragePointer
    )
{
#if _AMD64_
    __writegsqword(0x58, (ULONG_PTR)ThreadLocalStoragePointer);
#elif _X86_
    __writefsdword(0x18, (ULONG_PTR)ThreadLocalStoragePointer);
#elif _ARM_
    *(PVOID *)(_MoveFromCoprocessor(15, 0, 13, 0, 2) + 0x18) = ThreadLocalStoragePointer;
#elif _ARM64_
    *(PVOID *)(__getReg(18) + 0x58) = ThreadLocalStoragePointer;
#endif
}

_Function_class_(KDEFERRED_ROUTINE)
VOID
CrtpSetTebThreadLocalStoragePointerDpcRoutine (
    _In_ struct _KDPC* Dpc,
    _In_opt_ PVOID DeferredContext,
    _In_ PVOID ThreadLocalStoragePointer,
    _In_ PVOID RemainingCount
    )
{
    UNREFERENCED_PARAMETER(Dpc);
    UNREFERENCED_PARAMETER(DeferredContext);

    CrtSysSetTebThreadLocalStoragePointer(ThreadLocalStoragePointer);
    InterlockedDecrement((PLONG)RemainingCount);
}

NTSTATUS
CrtSysInitializeTebThreadLocalStoragePointer (
    VOID
    )
{
    KdBreakPoint();

    LONG remainingCount = KeNumberProcessors - 1;
    if (remainingCount == 0) {
        CrtSysSetTebThreadLocalStoragePointer(&CrtSyspTlsSlots);
        return STATUS_SUCCESS;
    }

    PKDPC dpcs = (PKDPC)ExAllocatePoolWithTag( NonPagedPool,
                                               sizeof(KDPC) * ((SIZE_T)KeNumberProcessors - 1),
                                               'pmeT' );
    if (dpcs == NULL) {
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    KIRQL oldIrql = KeRaiseIrqlToDpcLevel();
    CCHAR currentProcessor = (CCHAR)KeGetCurrentProcessorNumber();
    PKDPC dpc = dpcs;
    for (CCHAR i = 0; i < KeNumberProcessors; i++) {
        if (i == currentProcessor) {
            continue;
        }
        KeInitializeDpc(dpc, CrtpSetTebThreadLocalStoragePointerDpcRoutine, NULL);
        KeSetTargetProcessorDpc(dpc, i);
        KeInsertQueueDpc(dpc, &CrtSyspTlsSlots, &remainingCount);
        dpc++;
    }
    CrtSysSetTebThreadLocalStoragePointer(&CrtSyspTlsSlots);
    while (InterlockedCompareExchange(&remainingCount, 0, 0) != 0) {
        YieldProcessor();
    }
    KeLowerIrql(oldIrql);

    ExFreePoolWithTag( dpcs,
                       'pmeT' );

    return STATUS_SUCCESS;
}



#include <vcstartup_internal.h>

NTSTATUS
CrtSysDispatchRoutine (
    _In_ PDEVICE_OBJECT DeviceObject,
    _Inout_ PIRP Irp
    )
{
#if CRTSYS_USE_NTL_MAIN
  // clang-format on
  if (this_driver) {
    auto dev = this_driver->devices(DeviceObject);
    if (dev)
      return ntl::device_dispatch_invoker::invoke(*dev.get(), Irp);
  }
#else
  PIO_STACK_LOCATION IrpSp = IoGetCurrentIrpStackLocation(Irp);
  switch (IrpSp->MajorFunction) {
  case IRP_MJ_CREATE:
    if (CrtsyspDispatchCreate)
      return CrtsyspDispatchCreate(DeviceObject, Irp);
    break;

  case IRP_MJ_CLOSE:
    if (CrtsyspDispatchClose)
      return CrtsyspDispatchClose(DeviceObject, Irp);
    break;
  case IRP_MJ_DEVICE_CONTROL:
    if (CrtsyspDispatchDeviceControl)
      return CrtsyspDispatchDeviceControl(DeviceObject, Irp);
    break;
  }
#endif
  Irp->IoStatus.Status = STATUS_INVALID_DEVICE_REQUEST;
  IoCompleteRequest(Irp, IO_NO_INCREMENT);
  return STATUS_INVALID_DEVICE_REQUEST;
}

namespace ntl {
class driver_initializer {
public:
  static std::unique_ptr<ntl::driver> make_driver(PDRIVER_OBJECT object) {
    return std::make_unique<ntl::driver>(std::move(ntl::driver(object)));
  }
};
} // namespace ntl

EXTERN_C
NTSTATUS
CrtSysDriverEntry(_In_ PDRIVER_OBJECT DriverObject,
                  _In_ PUNICODE_STRING RegistryPath) {
  PAGED_CODE();

  NTSTATUS status = CrtSysInitializeTebThreadLocalStoragePointer();
  if (!NT_SUCCESS(status)) {
    return status;
  }

  status = LdkInitialize(DriverObject, RegistryPath, 0);
  if (!NT_SUCCESS(status)) {
    return status;
  }

  NTSTATUS
  CrtSysInitializeFlsXState(VOID);
  status = CrtSysInitializeFlsXState();
  if (!NT_SUCCESS(status)) {
    LdkTerminate();
    return status;
  }
#if CRTSYS_USE_LIBCNTPR
  NTSTATUS
  CrtSyspInitializeForLibcntpr(VOID);
  status = CrtSyspInitializeForLibcntpr();
  if (!NT_SUCCESS(status)) {
    LdkTerminate();
    return status;
  }
#endif
  if (!__scrt_initialize_onexit_tables(__scrt_module_type::exe)) {
    KdBreakPoint();
    LdkTerminate();
    return STATUS_FAILED_DRIVER_ENTRY;
  }

  if (!__scrt_initialize_crt(__scrt_module_type::exe)) {
    KdBreakPoint();
    LdkTerminate();
    return STATUS_FAILED_DRIVER_ENTRY;
  }

  __scrt_current_native_startup_state =
      __scrt_native_startup_state::initializing;

  if (_initterm_e(__xi_a, __xi_z) != 0) {
    CrtSysDriverUnload(DriverObject);
    return STATUS_FAILED_DRIVER_ENTRY;
  }

  _initterm(__xc_a, __xc_z);

  __scrt_current_native_startup_state =
      __scrt_native_startup_state::initialized;

#if CRTSYS_USE_NTL_MAIN
  // clang-format on

  auto driver = ntl::driver_initializer::make_driver(DriverObject);

  if (!driver) {
    return STATUS_INSUFFICIENT_RESOURCES;
  }
  ntl::status s = ntl::expand_stack(ntl::main, std::ref(*driver.get()),
                                    std::wstring(RegistryPath->Buffer));
  if (!s.is_ok()) {
    CrtSysDriverUnload(DriverObject);
    return s;
  }
  this_driver = std::move(driver);
// clang-format off
#else
    status = DriverEntry( DriverObject,
                          RegistryPath );
    if (!NT_SUCCESS(status)) {
        CrtSysDriverUnload( DriverObject );
        return status;
    }
    CrtsyspDriverUnload = DriverObject->DriverUnload;
    CrtsyspDispatchCreate = DriverObject->MajorFunction[IRP_MJ_CREATE];
    CrtsyspDispatchClose = DriverObject->MajorFunction[IRP_MJ_CLOSE];
    CrtsyspDispatchDeviceControl = DriverObject->MajorFunction[IRP_MJ_DEVICE_CONTROL];
#endif
    for (int i = 0; i < IRP_MJ_MAXIMUM_FUNCTION; i++)
        DriverObject->MajorFunction[i] = CrtSysDispatchRoutine;
    DriverObject->DriverUnload = CrtSysDriverUnload;
    return status;
}

#if CRTSYS_USE_NTL_MAIN
// clang-format on
namespace ntl {
class driver_unload_invoker {
public:
  static void unload(driver &driver) {
    if (driver.unload_routine_)
      driver.unload_routine_();
  }
};
} // namespace ntl
  // clang-format off
#endif

EXTERN_C
VOID
CrtSysDriverUnload (
    _In_ PDRIVER_OBJECT DriverObject
    )
{
    PAGED_CODE();
#if CRTSYS_USE_NTL_MAIN
  // clang-format on
  UNREFERENCED_PARAMETER(DriverObject);

  if (this_driver)
    ntl::driver_unload_invoker::unload(*this_driver.get());

    // clang-format off
#else
    if (CrtsyspDriverUnload) {
        CrtsyspDriverUnload( DriverObject );
    }
#endif

    _cexit();

    __scrt_uninitialize_crt(
#if DBG
        false,
#else
        true,
#endif
        false);

#if CRTSYS_USE_LIBCNTPR
    VOID
    CrtSyspUninitializeForLibcntpr (
        VOID
        );
    CrtSyspUninitializeForLibcntpr();
#endif
    LdkTerminate();
}