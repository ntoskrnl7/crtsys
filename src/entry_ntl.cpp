#include "runtime_internal.h"

#include "../include/ntl/driver"
#include "../include/ntl/expand_stack"

#include <memory>

EXTERN_C DRIVER_INITIALIZE CrtSysDriverEntry;
EXTERN_C DRIVER_UNLOAD CrtSysDriverUnload;

#ifdef ALLOC_PRAGMA
#pragma alloc_text(INIT, CrtSysDriverEntry)
#pragma alloc_text(PAGE, CrtSysDriverUnload)
#endif

namespace {
std::unique_ptr<ntl::driver> this_driver;
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

class device_dispatch_invoker {
public:
  static status invoke(PDEVICE_OBJECT device_object, PIRP irp) noexcept {
    NTSTATUS status = STATUS_INVALID_DEVICE_REQUEST;
    bool complete_irp = true;
    irp->IoStatus.Information = 0;
    auto dispatchers = this_driver->dispatchers(device_object);
    if (dispatchers) {
      bool has_any_dispatcher = dispatchers->on_create ||
                                 dispatchers->on_close ||
                                 dispatchers->on_device_control ||
                                 dispatchers->on_pending_device_control;
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
        if (dispatchers->on_device_control ||
            dispatchers->on_pending_device_control) {
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
            case METHOD_NEITHER:
              ProbeForRead(irp_sp->Parameters.DeviceIoControl.Type3InputBuffer,
                           irp_sp->Parameters.DeviceIoControl.InputBufferLength,
                           sizeof(UCHAR));
              in_buf_ptr = irp_sp->Parameters.DeviceIoControl.Type3InputBuffer;
              out_buf_ptr = irp->UserBuffer;
              break;
            default:
              throw ntl::exception(STATUS_INVALID_DEVICE_REQUEST,
                                   "Invalid control code method");
            }
            device_control::code code(
                irp_sp->Parameters.DeviceIoControl.IoControlCode);
            device_control::in_buffer in_buf(
                in_buf_ptr,
                irp_sp->Parameters.DeviceIoControl.InputBufferLength);
            device_control::out_buffer out_buf(out_buf_ptr, out_len);
            if (dispatchers->on_pending_device_control) {
              ntl::irp request(irp);
              const auto result = dispatchers->on_pending_device_control(
                  request, code, in_buf, out_buf);
              if (result == device_control::dispatch_result::pending) {
                status = STATUS_PENDING;
                complete_irp = false;
                return;
              }
            } else {
              dispatchers->on_device_control(code, in_buf, out_buf);
            }
            irp->IoStatus.Information = static_cast<ULONG_PTR>(out_buf.size);
          });
        }
        break;
      default:
        break;
      }
    }
    if (!complete_irp)
      return STATUS_PENDING;
    irp->IoStatus.Status = status;
    IoCompleteRequest(irp, IO_NO_INCREMENT);
    return status;
  }
};
} // namespace ntl

static NTSTATUS CrtSysNtlDispatchRoutine(PDEVICE_OBJECT device_object,
                                         PIRP irp) {
  if (this_driver)
    return ntl::device_dispatch_invoker::invoke(device_object, irp);

  irp->IoStatus.Status = STATUS_INVALID_DEVICE_REQUEST;
  irp->IoStatus.Information = 0;
  IoCompleteRequest(irp, IO_NO_INCREMENT);
  return STATUS_INVALID_DEVICE_REQUEST;
}

EXTERN_C
NTSTATUS
CrtSysDriverEntry(PDRIVER_OBJECT driver_object,
                  PUNICODE_STRING registry_path) {
  PAGED_CODE();

  NTSTATUS status = CrtSysInitializeRuntime(driver_object, registry_path);
  if (!NT_SUCCESS(status))
    return status;

  try {
    auto driver = ntl::driver_initializer::make_driver(driver_object);
    ntl::status entry_status = ntl::expand_stack(
        ntl::main, std::ref(*driver), std::wstring(registry_path->Buffer));
    if (!entry_status.is_ok()) {
      ntl::driver_unload_invoker::unload(*driver);
      driver.reset();
      CrtSysUninitializeRuntime(driver_object);
      return entry_status;
    }
    this_driver = std::move(driver);
  } catch (const ntl::exception &e) {
    CrtSysUninitializeRuntime(driver_object);
    return e.get_status();
  } catch (...) {
    CrtSysUninitializeRuntime(driver_object);
    return STATUS_UNSUCCESSFUL;
  }

  for (int i = 0; i < IRP_MJ_MAXIMUM_FUNCTION; ++i)
    driver_object->MajorFunction[i] = CrtSysNtlDispatchRoutine;
  driver_object->DriverUnload = CrtSysDriverUnload;
  return STATUS_SUCCESS;
}

EXTERN_C
VOID
CrtSysDriverUnload(PDRIVER_OBJECT driver_object) {
  PAGED_CODE();

  if (this_driver)
    ntl::driver_unload_invoker::unload(*this_driver);

  this_driver.reset();
  CrtSysUninitializeRuntime(driver_object);
}
