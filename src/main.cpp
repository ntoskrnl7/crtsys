// clang-format off

#include <Ldk/windows.h>

#include <corecrt_startup.h>
#include <vcruntime_internal.h>
#include <Ldk/ldk.h>
#include "runtime_internal.h"

namespace ntl {
namespace detail {
namespace resource {
extern NPAGED_LOOKASIDE_LIST lookaside;
}
namespace spin_lock {
extern NPAGED_LOOKASIDE_LIST lookaside;
}
} // namespace detail
} // namespace ntl

EXTERN_C NTSYSAPI PVOID NTAPI RtlImageDirectoryEntryToData(
    _In_ PVOID Base,
    _In_ BOOLEAN MappedAsImage,
    _In_ USHORT DirectoryEntry,
    _Out_ PULONG Size
    );
EXTERN_C int __cdecl _initialize_narrow_environment();
EXTERN_C int __cdecl _initialize_wide_environment();
EXTERN_C NTSTATUS CrtSysInitializeFlsXState(VOID);
#if CRTSYS_USE_LIBCNTPR
EXTERN_C NTSTATUS CrtSyspInitializeForLibcntpr(VOID);
EXTERN_C VOID CrtSyspUninitializeForLibcntpr(VOID);
#endif
void __cdecl __scrt_initialize_type_info();
void __cdecl __scrt_uninitialize_type_info();

#ifdef ALLOC_PRAGMA
#pragma alloc_text(INIT, CrtSysInitializeRuntime)
#pragma alloc_text(PAGE, CrtSysUninitializeRuntime)
#endif

//
// MSVC emits compiler TLS accesses for thread-safe local statics. Kernel
// drivers do not have a user-mode loader-managed TEB TLS vector. crtsys is a
// static library, so every consuming driver image gets its own copy of this
// runtime. To keep multiple crtsys-linked drivers from colliding, all images
// share one system-space TLS slot vector and allocate a unique MSVC _tls_index
// from it. Each driver still owns its own TLS image buffer.
//
#define CRTSYS_COMPILER_TLS_SLOT_COUNT 1024
#define CRTSYS_COMPILER_TLS_BITMAP_COUNT (CRTSYS_COMPILER_TLS_SLOT_COUNT / 32)
#define CRTSYS_COMPILER_TLS_INDEX_INVALID ((ULONG)-1)
#define CRTSYS_COMPILER_TLS_SECTION_MAGIC 0x4353544cu
#define CRTSYS_COMPILER_TLS_SECTION_VERSION 1u

typedef struct _CRTSYS_SHARED_COMPILER_TLS_RUNTIME {
    volatile LONG InitState;
    ULONG Magic;
    ULONG Version;
    ULONG Size;
    volatile LONG ReferenceCount;
    PVOID CanonicalSlots;
    KSPIN_LOCK Lock;
    ULONG SlotBitmap[CRTSYS_COMPILER_TLS_BITMAP_COUNT];
    PVOID Slots[CRTSYS_COMPILER_TLS_SLOT_COUNT];
} CRTSYS_SHARED_COMPILER_TLS_RUNTIME, *PCRTSYS_SHARED_COMPILER_TLS_RUNTIME;

__declspec(align(16)) UCHAR CrtSyspCompilerTlsBuffer[1024 * 1024];
PCRTSYS_SHARED_COMPILER_TLS_RUNTIME CrtSyspCompilerTlsRuntime = NULL;
PVOID CrtSyspCompilerTlsRuntimeViewBase = NULL;
PVOID *CrtSyspCompilerTlsSlots = NULL;
HANDLE CrtSyspCompilerTlsSectionHandle = NULL;
ULONG CrtSyspCompilerTlsIndex = CRTSYS_COMPILER_TLS_INDEX_INVALID;
BOOLEAN CrtSyspTypeInfoInitialized = FALSE;

NTSTATUS
CrtSysMapSharedCompilerTlsRuntime (
    VOID
    )
{
    if (CrtSyspCompilerTlsRuntime != NULL) {
        return STATUS_SUCCESS;
    }

    UNICODE_STRING sectionName;
    RtlInitUnicodeString(&sectionName,
                         L"\\KernelObjects\\CrtSysCompilerTlsRuntime_v1");

    OBJECT_ATTRIBUTES objectAttributes;
    InitializeObjectAttributes(&objectAttributes,
                               &sectionName,
                               OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE,
                               NULL,
                               NULL);

    LARGE_INTEGER maximumSize;
    maximumSize.QuadPart = sizeof(CRTSYS_SHARED_COMPILER_TLS_RUNTIME);

    HANDLE sectionHandle = NULL;
    NTSTATUS status = ZwCreateSection(&sectionHandle,
                                      SECTION_ALL_ACCESS,
                                      &objectAttributes,
                                      &maximumSize,
                                      PAGE_READWRITE,
                                      SEC_COMMIT,
                                      NULL);
    if (status == STATUS_OBJECT_NAME_COLLISION) {
        status = ZwOpenSection(&sectionHandle,
                               SECTION_ALL_ACCESS,
                               &objectAttributes);
    }

    if (!NT_SUCCESS(status)) {
        return status;
    }

    PVOID sectionObject = NULL;
    status = ObReferenceObjectByHandle(sectionHandle,
                                       SECTION_MAP_READ | SECTION_MAP_WRITE,
                                       NULL,
                                       KernelMode,
                                       &sectionObject,
                                       NULL);
    if (!NT_SUCCESS(status)) {
        ZwClose(sectionHandle);
        return status;
    }

    PVOID viewBase = NULL;
    SIZE_T viewSize = 0;
    status = MmMapViewInSystemSpace(sectionObject, &viewBase, &viewSize);
    ObDereferenceObject(sectionObject);
    if (!NT_SUCCESS(status)) {
        ZwClose(sectionHandle);
        return status;
    }

    PCRTSYS_SHARED_COMPILER_TLS_RUNTIME runtime =
        (PCRTSYS_SHARED_COMPILER_TLS_RUNTIME)viewBase;

    LONG initState = InterlockedCompareExchange(&runtime->InitState, 1, 0);
    if (initState == 0) {
        runtime->Magic = CRTSYS_COMPILER_TLS_SECTION_MAGIC;
        runtime->Version = CRTSYS_COMPILER_TLS_SECTION_VERSION;
        runtime->Size = sizeof(*runtime);
        runtime->ReferenceCount = 0;
        runtime->CanonicalSlots = runtime->Slots;
        KeInitializeSpinLock(&runtime->Lock);
        RtlZeroMemory(runtime->SlotBitmap, sizeof(runtime->SlotBitmap));
        RtlZeroMemory(runtime->Slots, sizeof(runtime->Slots));
        InterlockedExchange(&runtime->InitState, 2);
    } else {
        for (ULONG waitCount = 0;
             InterlockedCompareExchange(&runtime->InitState, 2, 2) != 2;
             ++waitCount) {
            if (waitCount == 10000000u) {
                MmUnmapViewInSystemSpace(viewBase);
                ZwClose(sectionHandle);
                return STATUS_IO_TIMEOUT;
            }
            YieldProcessor();
        }
    }

    if (runtime->Magic != CRTSYS_COMPILER_TLS_SECTION_MAGIC ||
        runtime->Version != CRTSYS_COMPILER_TLS_SECTION_VERSION ||
        runtime->Size != sizeof(*runtime) ||
        runtime->CanonicalSlots == NULL) {
        MmUnmapViewInSystemSpace(viewBase);
        ZwClose(sectionHandle);
        return STATUS_REVISION_MISMATCH;
    }

    CrtSyspCompilerTlsRuntime = runtime;
    CrtSyspCompilerTlsRuntimeViewBase = viewBase;
    CrtSyspCompilerTlsSlots = (PVOID *)runtime->CanonicalSlots;
    CrtSyspCompilerTlsSectionHandle = sectionHandle;
    InterlockedIncrement(&runtime->ReferenceCount);

    return STATUS_SUCCESS;
}

NTSTATUS
CrtSysAllocateSharedCompilerTlsSlot (
    _In_ PVOID CompilerTlsBuffer,
    _Out_ PULONG SlotIndex
    )
{
    if (CrtSyspCompilerTlsRuntime == NULL) {
        return STATUS_INVALID_DEVICE_STATE;
    }

    KIRQL oldIrql;
    KeAcquireSpinLock(&CrtSyspCompilerTlsRuntime->Lock, &oldIrql);

    NTSTATUS status = STATUS_INSUFFICIENT_RESOURCES;
    for (ULONG wordIndex = 0; wordIndex < CRTSYS_COMPILER_TLS_BITMAP_COUNT;
         ++wordIndex) {
        ULONG bits = CrtSyspCompilerTlsRuntime->SlotBitmap[wordIndex];
        if (bits == 0xFFFFFFFFu) {
            continue;
        }

        for (ULONG bitIndex = 0; bitIndex < 32; ++bitIndex) {
            ULONG const mask = 1u << bitIndex;
            if ((bits & mask) != 0) {
                continue;
            }

            ULONG const slotIndex = wordIndex * 32 + bitIndex;
            CrtSyspCompilerTlsRuntime->SlotBitmap[wordIndex] = bits | mask;
            CrtSyspCompilerTlsRuntime->Slots[slotIndex] = CompilerTlsBuffer;
            *SlotIndex = slotIndex;
            status = STATUS_SUCCESS;
            goto Exit;
        }
    }

Exit:
    KeReleaseSpinLock(&CrtSyspCompilerTlsRuntime->Lock, oldIrql);
    return status;
}

VOID
CrtSysReleaseSharedCompilerTlsSlot (
    VOID
    )
{
    if (CrtSyspCompilerTlsRuntime == NULL ||
        CrtSyspCompilerTlsIndex == CRTSYS_COMPILER_TLS_INDEX_INVALID) {
        return;
    }

    ULONG const slotIndex = CrtSyspCompilerTlsIndex;
    KIRQL oldIrql;
    KeAcquireSpinLock(&CrtSyspCompilerTlsRuntime->Lock, &oldIrql);

    if (slotIndex < CRTSYS_COMPILER_TLS_SLOT_COUNT &&
        CrtSyspCompilerTlsRuntime->Slots[slotIndex] == CrtSyspCompilerTlsBuffer) {
        CrtSyspCompilerTlsRuntime->Slots[slotIndex] = NULL;
        CrtSyspCompilerTlsRuntime->SlotBitmap[slotIndex / 32] &=
            ~(1u << (slotIndex % 32));
    }

    KeReleaseSpinLock(&CrtSyspCompilerTlsRuntime->Lock, oldIrql);
    CrtSyspCompilerTlsIndex = CRTSYS_COMPILER_TLS_INDEX_INVALID;
}

VOID
CrtSysUnmapSharedCompilerTlsRuntime (
    VOID
    )
{
    if (CrtSyspCompilerTlsRuntimeViewBase == NULL ||
        CrtSyspCompilerTlsRuntime == NULL) {
        return;
    }

    PVOID const viewBase = CrtSyspCompilerTlsRuntimeViewBase;
    PVOID const canonicalBase =
        (PUCHAR)CrtSyspCompilerTlsRuntime->CanonicalSlots -
        FIELD_OFFSET(CRTSYS_SHARED_COMPILER_TLS_RUNTIME, Slots);
    LONG const remainingReferences =
        InterlockedDecrement(&CrtSyspCompilerTlsRuntime->ReferenceCount);
    HANDLE const sectionHandle = CrtSyspCompilerTlsSectionHandle;

    //
    // Keep the first mapping alive while any crtsys-linked driver still uses
    // the shared slots vector. If the owner of the canonical mapping unloads
    // first, later drivers keep using that mapping until the shared refcount
    // reaches zero.
    //
    if (viewBase != canonicalBase) {
        MmUnmapViewInSystemSpace(viewBase);
    }

    if (remainingReferences == 0) {
        MmUnmapViewInSystemSpace(canonicalBase);
    }

    CrtSyspCompilerTlsRuntime = NULL;
    CrtSyspCompilerTlsRuntimeViewBase = NULL;
    CrtSyspCompilerTlsSlots = NULL;
    CrtSyspCompilerTlsSectionHandle = NULL;

    if (sectionHandle != NULL) {
        ZwClose(sectionHandle);
    }
}

VOID
CrtSysUninitializeCompilerTlsImage (
    VOID
    )
{
    CrtSysReleaseSharedCompilerTlsSlot();
    CrtSysUnmapSharedCompilerTlsRuntime();
}

EXTERN_C
ULONG
CrtSysGetCompilerTlsIndexForDiagnostics (
    VOID
    )
{
    // Test diagnostic export. This is intentionally not a public crtsys API.
    return CrtSyspCompilerTlsIndex;
}

EXTERN_C
VOID
CrtSysGetCompilerTlsRuntimeForDiagnostics (
    _Out_ PULONG SlotBitmap0,
    _Out_ PVOID *Runtime,
    _Out_ PVOID *CanonicalSlots,
    _Out_ PVOID *InstalledSlots
    )
{
    // Test diagnostic export. This is intentionally not a public crtsys API.
    if (SlotBitmap0 != NULL) {
        *SlotBitmap0 = CrtSyspCompilerTlsRuntime != NULL
                           ? CrtSyspCompilerTlsRuntime->SlotBitmap[0]
                           : 0;
    }
    if (Runtime != NULL) {
        *Runtime = CrtSyspCompilerTlsRuntime;
    }
    if (CanonicalSlots != NULL) {
        *CanonicalSlots = CrtSyspCompilerTlsRuntime != NULL
                              ? CrtSyspCompilerTlsRuntime->CanonicalSlots
                              : NULL;
    }
    if (InstalledSlots != NULL) {
        *InstalledSlots = CrtSyspCompilerTlsSlots;
    }
}

NTSTATUS
CrtSysInitializeCompilerTlsImage (
    _In_ PDRIVER_OBJECT DriverObject
    )
{
    NTSTATUS status = CrtSysMapSharedCompilerTlsRuntime();
    if (!NT_SUCCESS(status)) {
        return status;
    }

    ULONG size = 0;
    PIMAGE_TLS_DIRECTORY tlsDirectory =
        (PIMAGE_TLS_DIRECTORY)RtlImageDirectoryEntryToData(
            DriverObject->DriverStart,
            TRUE,
            IMAGE_DIRECTORY_ENTRY_TLS,
            &size);

    RtlZeroMemory(CrtSyspCompilerTlsBuffer, sizeof(CrtSyspCompilerTlsBuffer));

    if (tlsDirectory == NULL) {
        return STATUS_SUCCESS;
    }
    if (size < sizeof(*tlsDirectory)) {
        CrtSysUninitializeCompilerTlsImage();
        return STATUS_INVALID_IMAGE_FORMAT;
    }

    ULONG_PTR const rawStart =
        (ULONG_PTR)tlsDirectory->StartAddressOfRawData;
    ULONG_PTR const rawEnd =
        (ULONG_PTR)tlsDirectory->EndAddressOfRawData;
    if (rawEnd < rawStart) {
        CrtSysUninitializeCompilerTlsImage();
        return STATUS_INVALID_IMAGE_FORMAT;
    }

    SIZE_T const rawSize = rawEnd - rawStart;
    SIZE_T const zeroFillSize = (SIZE_T)tlsDirectory->SizeOfZeroFill;
    if (rawSize > sizeof(CrtSyspCompilerTlsBuffer) ||
        zeroFillSize > sizeof(CrtSyspCompilerTlsBuffer) - rawSize) {
        CrtSysUninitializeCompilerTlsImage();
        return STATUS_BUFFER_TOO_SMALL;
    }

    if (rawSize != 0) {
        RtlCopyMemory(CrtSyspCompilerTlsBuffer, (PVOID)rawStart, rawSize);
    }

    if (tlsDirectory->AddressOfIndex == 0) {
        CrtSysUninitializeCompilerTlsImage();
        return STATUS_INVALID_IMAGE_FORMAT;
    }

    ULONG slotIndex = CRTSYS_COMPILER_TLS_INDEX_INVALID;
    status = CrtSysAllocateSharedCompilerTlsSlot(CrtSyspCompilerTlsBuffer,
                                                 &slotIndex);
    if (!NT_SUCCESS(status)) {
        CrtSysUninitializeCompilerTlsImage();
        return status;
    }

    *(PULONG)(ULONG_PTR)tlsDirectory->AddressOfIndex = slotIndex;
    CrtSyspCompilerTlsIndex = slotIndex;

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
        CrtSysUninitializeCompilerTlsImage();
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
        CrtSysUninitializeCompilerTlsImage();
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
CrtSysInitializeRuntime(_In_ PDRIVER_OBJECT DriverObject,
                        _In_ PUNICODE_STRING RegistryPath) {
  PAGED_CODE();

  NTSTATUS status = CrtSysInitializeTebThreadLocalStoragePointer(DriverObject);
  if (!NT_SUCCESS(status)) {
    return status;
  }

  status = LdkInitialize(DriverObject, RegistryPath, 0);
  if (!NT_SUCCESS(status)) {
    CrtSysUninitializeCompilerTlsImage();
    return status;
  }

  status = CrtSysInitializeFlsXState();
  if (!NT_SUCCESS(status)) {
    CrtSysUninitializeCompilerTlsImage();
    LdkTerminate();
    return status;
  }
#if CRTSYS_USE_LIBCNTPR
  status = CrtSyspInitializeForLibcntpr();
  if (!NT_SUCCESS(status)) {
    CrtSysUninitializeCompilerTlsImage();
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
#if CRTSYS_USE_LIBCNTPR
    CrtSyspUninitializeForLibcntpr();
#endif
    CrtSysUninitializeCompilerTlsImage();
    LdkTerminate();
    return STATUS_FAILED_DRIVER_ENTRY;
  }

  if (!__scrt_initialize_crt(__scrt_module_type::exe)) {
    CRTSYS_DIAGNOSTIC_BREAK();
    ExDeleteNPagedLookasideList(&ntl::detail::resource::lookaside);
    ExDeleteNPagedLookasideList(&ntl::detail::spin_lock::lookaside);
#if CRTSYS_USE_LIBCNTPR
    CrtSyspUninitializeForLibcntpr();
#endif
    CrtSysUninitializeCompilerTlsImage();
    LdkTerminate();
    return STATUS_FAILED_DRIVER_ENTRY;
  }
  __scrt_initialize_type_info();
  CrtSyspTypeInfoInitialized = TRUE;

  __scrt_current_native_startup_state =
      __scrt_native_startup_state::initializing;

  status = CrtSyspInitializeProgramPathState();
  if (!NT_SUCCESS(status)) {
    CrtSysUninitializeRuntime(DriverObject);
    return status;
  }

  status = CrtSyspInitializeEnvironment();
  if (!NT_SUCCESS(status)) {
    CrtSysUninitializeRuntime(DriverObject);
    return status;
  }

  if (_initterm_e(__xi_a, __xi_z) != 0) {
    CrtSysUninitializeRuntime(DriverObject);
    return STATUS_FAILED_DRIVER_ENTRY;
  }

  _initterm(__xc_a, __xc_z);

  __scrt_current_native_startup_state =
      __scrt_native_startup_state::initialized;

  return STATUS_SUCCESS;
}

EXTERN_C
VOID
CrtSysUninitializeRuntime (
    _In_ PDRIVER_OBJECT DriverObject
    )
{
    PAGED_CODE();
    UNREFERENCED_PARAMETER(DriverObject);

    _cexit();
    if (CrtSyspTypeInfoInitialized) {
      __scrt_uninitialize_type_info();
      CrtSyspTypeInfoInitialized = FALSE;
    }

    __scrt_uninitialize_crt(
#if DBG
        false,
#else
        true,
#endif
        false);

#if CRTSYS_USE_LIBCNTPR
    CrtSyspUninitializeForLibcntpr();
#endif
    ExDeleteNPagedLookasideList(&ntl::detail::resource::lookaside);
    ExDeleteNPagedLookasideList(&ntl::detail::spin_lock::lookaside);
    CrtSysUninitializeCompilerTlsImage();
    LdkTerminate();
}
