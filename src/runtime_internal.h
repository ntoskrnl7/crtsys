#pragma once

#include <ntddk.h>

EXTERN_C
NTSTATUS
CrtSysInitializeRuntime(
    _In_ PDRIVER_OBJECT DriverObject,
    _In_ PUNICODE_STRING RegistryPath
    );

EXTERN_C
VOID
CrtSysUninitializeRuntime(
    _In_ PDRIVER_OBJECT DriverObject
    );
