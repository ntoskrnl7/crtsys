#include <wdm.h>

#include <cstdint>
#include <cstring>
#include <memory>
#include <string>

#include <ntl/driver>

#include "common/multi_instance.h"

#ifndef CRTSYS_MULTI_INSTANCE_ID
#error CRTSYS_MULTI_INSTANCE_ID must be defined.
#endif

#ifndef WINAPI
#define WINAPI __stdcall
#endif

extern "C" ULONG WINAPI TlsAlloc();
extern "C" int WINAPI TlsFree(ULONG tls_index);
extern "C" int WINAPI TlsSetValue(ULONG tls_index, void *tls_value);
extern "C" void *WINAPI TlsGetValue(ULONG tls_index);
extern "C" ULONG WINAPI GetLastError();
extern "C" void WINAPI SetLastError(ULONG last_error);
extern "C" unsigned long CrtSysGetCompilerTlsIndexForDiagnostics();
extern "C" void CrtSysGetCompilerTlsRuntimeForDiagnostics(
    unsigned long *slot_bitmap0, void **runtime, void **canonical_slots,
    void **installed_slots);

#if CRTSYS_MULTI_INSTANCE_ID == 1
#define CRTSYS_MULTI_DEVICE_NAME CRTSYS_MULTI_DEVICE_A
#elif CRTSYS_MULTI_INSTANCE_ID == 2
#define CRTSYS_MULTI_DEVICE_NAME CRTSYS_MULTI_DEVICE_B
#else
#error Unsupported CRTSYS_MULTI_INSTANCE_ID.
#endif

namespace {

struct multi_instance_extension {
  ULONG tls_index = 0xFFFFFFFFu;
};

unsigned long make_local_static_value() {
  volatile unsigned long instance = CRTSYS_MULTI_INSTANCE_ID;
  return instance * 1000u + 7u;
}

unsigned long local_static_value() {
  // Keep this dynamically initialized so the multi-driver probe exercises
  // MSVC's thread-safe local-static runtime and crtsys compiler TLS state.
  static const unsigned long value = make_local_static_value();
  return value;
}

} // namespace

ntl::status ntl::main(ntl::driver &driver, const std::wstring &registry_path) {
  UNREFERENCED_PARAMETER(registry_path);

  const ULONG tls_index = TlsAlloc();
  if (tls_index == 0xFFFFFFFFu) {
    return ntl::status(STATUS_INSUFFICIENT_RESOURCES);
  }

  auto tls_owner =
      std::shared_ptr<void>(nullptr, [tls_index](void *) { TlsFree(tls_index); });

  auto device =
      driver.create_device<multi_instance_extension>(ntl::device_options()
                                                         .name(CRTSYS_MULTI_DEVICE_NAME)
                                                         .type(FILE_DEVICE_UNKNOWN)
                                                         .exclusive());
  if (!device) {
    return ntl::status(STATUS_UNSUCCESSFUL);
  }

  device->extension().tls_index = tls_index;

  std::weak_ptr device_weak = device;
  device->on_device_control(
      [device_weak](const ntl::device_control::code &code,
                    const ntl::device_control::in_buffer &in,
                    ntl::device_control::out_buffer &out) {
        if (code != CRTSYS_MULTI_INSTANCE_IOCTL || !in.ptr || !out.ptr ||
            in.size < sizeof(crtsys_multi_instance_request) ||
            out.size < sizeof(crtsys_multi_instance_response)) {
          out.size = 0;
          return;
        }

        const auto request =
            *reinterpret_cast<const crtsys_multi_instance_request *>(in.ptr);
        auto *response =
            reinterpret_cast<crtsys_multi_instance_response *>(out.ptr);

        auto device = device_weak.lock();
        if (!device) {
          out.size = 0;
          return;
        }

        if (request.command == CRTSYS_MULTI_INSTANCE_SET_QUERY) {
          TlsSetValue(device->extension().tls_index,
                      reinterpret_cast<void *>(
                          static_cast<std::uintptr_t>(request.tls_value)));
          SetLastError(request.last_error_value);
        } else if (request.command != CRTSYS_MULTI_INSTANCE_QUERY) {
          out.size = 0;
          return;
        }

        const ULONG last_error_value = GetLastError();
        void *const tls_value = TlsGetValue(device->extension().tls_index);
        SetLastError(last_error_value);

        std::memset(response, 0, sizeof(*response));
        response->instance_id = CRTSYS_MULTI_INSTANCE_ID;
        response->tls_index = device->extension().tls_index;
        response->thread =
            reinterpret_cast<unsigned __int64>(PsGetCurrentThread());
        response->tls_value =
            static_cast<unsigned __int64>(reinterpret_cast<std::uintptr_t>(
                tls_value));
        response->last_error_value = last_error_value;
        response->local_static_value = local_static_value();
        response->crtsys_compiler_tls_index =
            CrtSysGetCompilerTlsIndexForDiagnostics();
        response->compiler_tls_index = response->crtsys_compiler_tls_index;
        void *runtime = nullptr;
        void *canonical_slots = nullptr;
        void *installed_slots = nullptr;
        CrtSysGetCompilerTlsRuntimeForDiagnostics(
            &response->crtsys_compiler_tls_bitmap0, &runtime,
            &canonical_slots, &installed_slots);
        response->crtsys_compiler_tls_runtime =
            reinterpret_cast<unsigned __int64>(runtime);
        response->crtsys_compiler_tls_canonical_slots =
            reinterpret_cast<unsigned __int64>(canonical_slots);
        response->crtsys_compiler_tls_installed_slots =
            reinterpret_cast<unsigned __int64>(installed_slots);
        out.size = sizeof(*response);
      });

  driver.on_unload([device, tls_owner]() {
    UNREFERENCED_PARAMETER(device);
    UNREFERENCED_PARAMETER(tls_owner);
  });

  return ntl::status::ok();
}
