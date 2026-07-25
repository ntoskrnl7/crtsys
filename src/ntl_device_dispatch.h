#pragma once

#include "../include/ntl/driver"

namespace ntl {

/**
 * Internal bridge shared by the NTL WDM and NTL minifilter entry points.
 * Public drivers register typed callbacks through ntl::device; this class
 * performs the one native IRP dispatch needed by both driver models.
 */
class device_dispatch_invoker {
public:
  static status invoke(driver &owner, PDEVICE_OBJECT device_object,
                       PIRP irp) noexcept {
    NTSTATUS status = STATUS_INVALID_DEVICE_REQUEST;
    bool complete_irp = true;
    irp->IoStatus.Information = 0;
    auto dispatchers = owner.dispatchers(device_object);
    if (dispatchers) {
      const bool has_any_dispatcher =
          dispatchers->on_create || dispatchers->on_close ||
          dispatchers->on_cleanup || dispatchers->on_device_control ||
          dispatchers->on_pending_device_control;
      auto invoke_dispatch = [&](auto &&dispatch) {
        auto ret = ntl::seh::try_except([&]() {
          irp->IoStatus.Status = STATUS_SUCCESS;
          try {
            dispatch();
            status = irp->IoStatus.Status;
          } catch (const ntl::exception &error) {
            status = error.get_status();
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
      case IRP_MJ_CLEANUP: {
        if (dispatchers->on_cleanup) {
          ntl::irp request(irp);
          invoke_dispatch([&]() { dispatchers->on_cleanup(request); });
        } else if (has_any_dispatcher) {
          status = STATUS_SUCCESS;
        }
        break;
      }
      case IRP_MJ_DEVICE_CONTROL:
        if (dispatchers->on_device_control ||
            dispatchers->on_pending_device_control) {
          invoke_dispatch([&]() {
            const void *input = nullptr;
            void *output = nullptr;
            const size_t output_length =
                irp_sp->Parameters.DeviceIoControl.OutputBufferLength;
            switch (METHOD_FROM_CTL_CODE(
                irp_sp->Parameters.DeviceIoControl.IoControlCode)) {
            case METHOD_BUFFERED:
              input = irp->AssociatedIrp.SystemBuffer;
              output = irp->AssociatedIrp.SystemBuffer;
              break;
            case METHOD_IN_DIRECT:
            case METHOD_OUT_DIRECT:
              input = irp->AssociatedIrp.SystemBuffer;
              if (output_length != 0) {
                if (!irp->MdlAddress)
                  throw ntl::exception(STATUS_INVALID_USER_BUFFER,
                                       "direct IOCTL has no output MDL");
                output = MmGetSystemAddressForMdlSafe(
                    irp->MdlAddress, NormalPagePriority);
                if (!output)
                  throw ntl::exception(STATUS_INSUFFICIENT_RESOURCES,
                                       "unable to map direct IOCTL output");
              }
              break;
            case METHOD_NEITHER:
              if (irp_sp->Parameters.DeviceIoControl.InputBufferLength != 0) {
                ProbeForRead(
                    irp_sp->Parameters.DeviceIoControl.Type3InputBuffer,
                    irp_sp->Parameters.DeviceIoControl.InputBufferLength,
                    sizeof(UCHAR));
              }
              input =
                  irp_sp->Parameters.DeviceIoControl.Type3InputBuffer;
              output = irp->UserBuffer;
              break;
            default:
              throw ntl::exception(STATUS_INVALID_DEVICE_REQUEST,
                                   "invalid IOCTL transfer method");
            }

            device_control::code code(
                irp_sp->Parameters.DeviceIoControl.IoControlCode);
            device_control::in_buffer in_buffer(
                input,
                irp_sp->Parameters.DeviceIoControl.InputBufferLength);
            device_control::out_buffer out_buffer(output, output_length);
            if (dispatchers->on_pending_device_control) {
              ntl::irp request(irp);
              const auto result =
                  dispatchers->on_pending_device_control(
                      request, code, in_buffer, out_buffer);
              if (result == device_control::dispatch_result::pending) {
                status = STATUS_PENDING;
                complete_irp = false;
                return;
              }
            } else {
              dispatchers->on_device_control(code, in_buffer, out_buffer);
            }
            irp->IoStatus.Information =
                static_cast<ULONG_PTR>(out_buffer.size);
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
