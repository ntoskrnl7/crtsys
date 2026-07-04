// clang-format off

#include <Ldk/windows.h>

#include <corecrt_startup.h>
#include <vcruntime_internal.h>
#include <Ldk/ldk.h>

EXTERN_C DRIVER_INITIALIZE CrtSysDriverEntry;
EXTERN_C DRIVER_UNLOAD CrtSysDriverUnload;
EXTERN_C NTSYSAPI PVOID NTAPI RtlImageDirectoryEntryToData(
    _In_ PVOID Base,
    _In_ BOOLEAN MappedAsImage,
    _In_ USHORT DirectoryEntry,
    _Out_ PULONG Size
    );
EXTERN_C int __cdecl _initialize_narrow_environment();
EXTERN_C int __cdecl _initialize_wide_environment();

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
  static status invoke(PDEVICE_OBJECT device_object, PIRP irp) noexcept {
    NTSTATUS status = STATUS_INVALID_DEVICE_REQUEST;
    irp->IoStatus.Information = 0;
    auto dispatchers = this_driver->dispatchers(device_object);
    if (dispatchers) {
      bool has_any_dispatcher = dispatchers->on_create ||
                                dispatchers->on_close ||
                                dispatchers->on_device_control;
      auto invoke_dispatch = [&](auto &&dispatch) {
        auto ret = ntl::seh::try_except([&]() {
          irp->IoStatus.Status = STATUS_SUCCESS;
          try {
            dispatch();
            status = irp->IoStatus.Status;
          } catch (const ntl::exception &e) {
            status = e.get_status();
            irp->IoStatus.Information = 0;
          } catch (const std::exception &) {
            status = STATUS_UNSUCCESSFUL;
            irp->IoStatus.Information = 0;
          }
        });
        if (!std::get<0>(ret)) {
          status = std::get<1>(ret);
          irp->IoStatus.Information = 0;
        }
      };
      PIO_STACK_LOCATION irp_sp = IoGetCurrentIrpStackLocation(irp);
      switch (irp_sp->MajorFunction) {
      case IRP_MJ_CREATE: {
        if (dispatchers->on_create) {
          ntl::irp request(irp);
          invoke_dispatch([&]() { dispatchers->on_create(request); });
        } else if (has_any_dispatcher) {
          status = STATUS_SUCCESS;
        }
        break;
      }
      case IRP_MJ_CLOSE: {
        if (dispatchers->on_close) {
          ntl::irp request(irp);
          invoke_dispatch([&]() { dispatchers->on_close(request); });
        } else if (has_any_dispatcher) {
          status = STATUS_SUCCESS;
        }
        break;
      }
      case IRP_MJ_DEVICE_CONTROL:
        if (dispatchers->on_device_control) {
          invoke_dispatch([&]() {
            const void *in_buf_ptr;
            void *out_buf_ptr;
            size_t out_len =
                irp_sp->Parameters.DeviceIoControl.OutputBufferLength;
            switch (METHOD_FROM_CTL_CODE(
                irp_sp->Parameters.DeviceIoControl.IoControlCode)) {
              case METHOD_BUFFERED:
                in_buf_ptr = irp->AssociatedIrp.SystemBuffer;
                out_buf_ptr = irp->AssociatedIrp.SystemBuffer;
                break;
              case METHOD_IN_DIRECT:
              case METHOD_OUT_DIRECT:
                in_buf_ptr = reinterpret_cast<const uint8_t *>(
                    irp->AssociatedIrp.SystemBuffer);
                out_buf_ptr =
                    reinterpret_cast<uint8_t *>(MmGetSystemAddressForMdlSafe(
                        irp->MdlAddress, NormalPagePriority));
                break;
              case METHOD_NEITHER: {
                ProbeForRead(
                    irp_sp->Parameters.DeviceIoControl.Type3InputBuffer,
                    irp_sp->Parameters.DeviceIoControl.InputBufferLength,
                    sizeof(UCHAR));
                in_buf_ptr =
                    irp_sp->Parameters.DeviceIoControl.Type3InputBuffer;
                out_buf_ptr = irp->UserBuffer;
                break;
              }
              default:
                throw ntl::exception(STATUS_INVALID_DEVICE_REQUEST,
                                     "Invalid control code method");
                break;
            }
            device_control::code code(
                irp_sp->Parameters.DeviceIoControl.IoControlCode);
            device_control::in_buffer in_buf(
                in_buf_ptr,
                irp_sp->Parameters.DeviceIoControl.InputBufferLength);
            device_control::out_buffer out_buf(out_buf_ptr, out_len);
            dispatchers->on_device_control(code, in_buf, out_buf);
            irp->IoStatus.Information = (ULONG_PTR)out_buf.size;
          });
        }
        break;
      default:
        break;
      }
    }
    irp->IoStatus.Status = status;
    IoCompleteRequest(irp, IO_NO_INCREMENT);
    return status;
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
// MSVC emits compiler TLS accesses for thread-safe local statics. Kernel
// drivers do not have a user-mode TEB TLS vector, so install a driver-wide TLS
// image initialized from the PE TLS template. This is not full per-thread TLS;
// it is enough for compiler-managed data such as _Init_thread_epoch.
//
__declspec(align(16)) UCHAR CrtSyspCompilerTlsBuffer[1024 * 1024];
PVOID CrtSyspCompilerTlsSlots[1024] = { CrtSyspCompilerTlsBuffer, };

NTSTATUS
CrtSysInitializeCompilerTlsImage (
    _In_ PDRIVER_OBJECT DriverObject
    )
{
    ULONG size = 0;
    PIMAGE_TLS_DIRECTORY tlsDirectory =
        (PIMAGE_TLS_DIRECTORY)RtlImageDirectoryEntryToData(
            DriverObject->DriverStart,
            TRUE,
            IMAGE_DIRECTORY_ENTRY_TLS,
            &size);

    RtlZeroMemory(CrtSyspCompilerTlsBuffer, sizeof(CrtSyspCompilerTlsBuffer));
    CrtSyspCompilerTlsSlots[0] = CrtSyspCompilerTlsBuffer;

    if (tlsDirectory == NULL) {
        return STATUS_SUCCESS;
    }
    if (size < sizeof(*tlsDirectory)) {
        return STATUS_INVALID_IMAGE_FORMAT;
    }

    ULONG_PTR const rawStart =
        (ULONG_PTR)tlsDirectory->StartAddressOfRawData;
    ULONG_PTR const rawEnd =
        (ULONG_PTR)tlsDirectory->EndAddressOfRawData;
    if (rawEnd < rawStart) {
        return STATUS_INVALID_IMAGE_FORMAT;
    }

    SIZE_T const rawSize = rawEnd - rawStart;
    SIZE_T const zeroFillSize = (SIZE_T)tlsDirectory->SizeOfZeroFill;
    if (rawSize > sizeof(CrtSyspCompilerTlsBuffer) ||
        zeroFillSize > sizeof(CrtSyspCompilerTlsBuffer) - rawSize) {
        return STATUS_BUFFER_TOO_SMALL;
    }

    if (rawSize != 0) {
        RtlCopyMemory(CrtSyspCompilerTlsBuffer, (PVOID)rawStart, rawSize);
    }

    if (tlsDirectory->AddressOfIndex != 0) {
        *(PULONG)(ULONG_PTR)tlsDirectory->AddressOfIndex = 0;
    }

    return STATUS_SUCCESS;
}

VOID
CrtSysSetTebThreadLocalStoragePointer (
    _In_ PVOID ThreadLocalStoragePointer
    )
{
#if _AMD64_
    __writegsqword(0x58, (ULONG_PTR)ThreadLocalStoragePointer);
#elif _X86_
    __writefsdword(0x2C, (ULONG_PTR)ThreadLocalStoragePointer);
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
    _In_ PDRIVER_OBJECT DriverObject
    )
{
    CRTSYS_DIAGNOSTIC_BREAK();

    NTSTATUS status = CrtSysInitializeCompilerTlsImage(DriverObject);
    if (!NT_SUCCESS(status)) {
        return status;
    }

    LONG remainingCount = KeNumberProcessors - 1;
    if (remainingCount == 0) {
        CrtSysSetTebThreadLocalStoragePointer(CrtSyspCompilerTlsSlots);
        return STATUS_SUCCESS;
    }
#pragma warning(disable:4996)
    PKDPC dpcs = (PKDPC)ExAllocatePoolWithTag( NonPagedPool,
                                               sizeof(KDPC) * ((SIZE_T)KeNumberProcessors - 1),
                                               'pmeT' );
#pragma warning(default:4996)
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
        KeInsertQueueDpc(dpc, CrtSyspCompilerTlsSlots, &remainingCount);
        dpc++;
    }
    CrtSysSetTebThreadLocalStoragePointer(CrtSyspCompilerTlsSlots);
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
  if (this_driver)
    return ntl::device_dispatch_invoker::invoke(DeviceObject, Irp);
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

class driver_unload_invoker {
public:
  static void unload(driver &driver) {
    if (driver.unload_routine_)
      driver.unload_routine_();
  }
};
} // namespace ntl

namespace ntl {
namespace detail {
namespace resource {
NPAGED_LOOKASIDE_LIST lookaside;
}
namespace spin_lock {
NPAGED_LOOKASIDE_LIST lookaside;
}
} // namespace detail
} // namespace ntl

static NTSTATUS CrtSyspInitializeEnvironment() {
  // MSVC's exe startup initializes the selected UCRT environment before
  // running C initializers. crtsys owns the driver startup sequence, so do the
  // equivalent work explicitly before exposing getenv/_putenv_s to user code.
  if (_initialize_narrow_environment() < 0) {
    return STATUS_UNSUCCESSFUL;
  }

  if (_initialize_wide_environment() < 0) {
    return STATUS_UNSUCCESSFUL;
  }

  return STATUS_SUCCESS;
}

static NTSTATUS CrtSyspInitializeProgramPathState() {
  // MSVC's exe startup configures argv and the _pgmptr/_wpgmptr program-path
  // globals before user code. Kernel drivers do not have a process command
  // line, but GetModuleFileName-backed program-path state is still meaningful.
  if (_configure_narrow_argv(_crt_argv_unexpanded_arguments) != 0) {
    return STATUS_UNSUCCESSFUL;
  }

  if (_configure_wide_argv(_crt_argv_unexpanded_arguments) != 0) {
    return STATUS_UNSUCCESSFUL;
  }

  return STATUS_SUCCESS;
}

EXTERN_C
NTSTATUS
CrtSysDriverEntry(_In_ PDRIVER_OBJECT DriverObject,
                  _In_ PUNICODE_STRING RegistryPath) {
  PAGED_CODE();

  NTSTATUS status = CrtSysInitializeTebThreadLocalStoragePointer(DriverObject);
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

  ExInitializeNPagedLookasideList(&ntl::detail::resource::lookaside, NULL, NULL,
                                  0, sizeof(ERESOURCE), 'srsc', 0);
  ExInitializeNPagedLookasideList(&ntl::detail::spin_lock::lookaside, NULL,
                                  NULL, 0, sizeof(KSPIN_LOCK), 'lpsc', 0);
  if (!__scrt_initialize_onexit_tables(__scrt_module_type::exe)) {
    CRTSYS_DIAGNOSTIC_BREAK();
    ExDeleteNPagedLookasideList(&ntl::detail::resource::lookaside);
    ExDeleteNPagedLookasideList(&ntl::detail::spin_lock::lookaside);
    LdkTerminate();
    return STATUS_FAILED_DRIVER_ENTRY;
  }

  if (!__scrt_initialize_crt(__scrt_module_type::exe)) {
    CRTSYS_DIAGNOSTIC_BREAK();
    ExDeleteNPagedLookasideList(&ntl::detail::resource::lookaside);
    ExDeleteNPagedLookasideList(&ntl::detail::spin_lock::lookaside);
    LdkTerminate();
    return STATUS_FAILED_DRIVER_ENTRY;
  }

  __scrt_current_native_startup_state =
      __scrt_native_startup_state::initializing;

  status = CrtSyspInitializeProgramPathState();
  if (!NT_SUCCESS(status)) {
    CrtSysDriverUnload(DriverObject);
    return status;
  }

  status = CrtSyspInitializeEnvironment();
  if (!NT_SUCCESS(status)) {
    CrtSysDriverUnload(DriverObject);
    return status;
  }

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
    CrtSysDriverUnload(DriverObject);
    return STATUS_INSUFFICIENT_RESOURCES;
  }
  ntl::status s = ntl::expand_stack(ntl::main, std::ref(*driver.get()),
                                    std::wstring(RegistryPath->Buffer));
  if (!s.is_ok()) {
    ntl::driver_unload_invoker::unload(*driver.get());
    driver.reset();
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
    ExDeleteNPagedLookasideList(&ntl::detail::resource::lookaside);
    ExDeleteNPagedLookasideList(&ntl::detail::spin_lock::lookaside);
    LdkTerminate();
}
