#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include <windows.h>

#include <setupapi.h>
#include <wbemidl.h>
#include <winioctl.h>

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cwchar>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include <ntl/handle>

#include "kmdf_wmi_ntl.hpp"

namespace {

template <class T> class com_ptr {
public:
  com_ptr() noexcept = default;
  com_ptr(const com_ptr &) = delete;
  com_ptr &operator=(const com_ptr &) = delete;

  com_ptr(com_ptr &&other) noexcept
      : value_(std::exchange(other.value_, nullptr)) {}

  com_ptr &operator=(com_ptr &&other) noexcept {
    if (this != &other) {
      reset();
      value_ = std::exchange(other.value_, nullptr);
    }
    return *this;
  }

  ~com_ptr() { reset(); }

  T *get() const noexcept { return value_; }
  T **put() noexcept {
    reset();
    return &value_;
  }
  T *operator->() const noexcept { return value_; }
  explicit operator bool() const noexcept { return value_ != nullptr; }

  void reset() noexcept {
    if (value_ != nullptr) {
      value_->Release();
      value_ = nullptr;
    }
  }

private:
  T *value_ = nullptr;
};

class bstr {
public:
  explicit bstr(const wchar_t *value) : value_(SysAllocString(value)) {
    if (value_ == nullptr)
      throw std::bad_alloc{};
  }
  bstr(const bstr &) = delete;
  bstr &operator=(const bstr &) = delete;
  ~bstr() { SysFreeString(value_); }

  operator BSTR() const noexcept { return value_; }

private:
  BSTR value_ = nullptr;
};

class variant {
public:
  variant() noexcept { VariantInit(&value_); }
  variant(const variant &) = delete;
  variant &operator=(const variant &) = delete;
  ~variant() { VariantClear(&value_); }

  VARIANT *put() noexcept {
    VariantClear(&value_);
    VariantInit(&value_);
    return &value_;
  }
  VARIANT &get() noexcept { return value_; }
  const VARIANT &get() const noexcept { return value_; }

private:
  VARIANT value_{};
};

class com_apartment {
public:
  com_apartment() noexcept = default;
  com_apartment(const com_apartment &) = delete;
  com_apartment &operator=(const com_apartment &) = delete;
  ~com_apartment() { CoUninitialize(); }
};

class device_info_set {
public:
  explicit device_info_set(HDEVINFO value) noexcept : value_(value) {}
  device_info_set(const device_info_set &) = delete;
  device_info_set &operator=(const device_info_set &) = delete;
  ~device_info_set() {
    if (value_ != INVALID_HANDLE_VALUE)
      SetupDiDestroyDeviceInfoList(value_);
  }

  HDEVINFO get() const noexcept { return value_; }
  explicit operator bool() const noexcept {
    return value_ != INVALID_HANDLE_VALUE;
  }

private:
  HDEVINFO value_ = INVALID_HANDLE_VALUE;
};

void check_hresult(HRESULT status, const char *operation) {
  if (FAILED(status)) {
    char message[160]{};
    std::snprintf(message, sizeof(message), "%s failed: 0x%08lx", operation,
                  static_cast<unsigned long>(status));
    throw std::runtime_error(message);
  }
}

std::uint32_t read_uint32(IWbemClassObject &object, const wchar_t *property) {
  variant value;
  check_hresult(object.Get(property, 0, value.put(), nullptr, nullptr),
                "IWbemClassObject::Get");
  if (value.get().vt == VT_UI4)
    return value.get().ulVal;
  if (value.get().vt == VT_I4)
    return static_cast<std::uint32_t>(value.get().lVal);
  throw std::runtime_error("WMI uint32 property has an unexpected type");
}

std::wstring read_string(IWbemClassObject &object, const wchar_t *property) {
  variant value;
  check_hresult(object.Get(property, 0, value.put(), nullptr, nullptr),
                "IWbemClassObject::Get");
  if (value.get().vt != VT_BSTR || value.get().bstrVal == nullptr)
    throw std::runtime_error("WMI string property has an unexpected type");
  return value.get().bstrVal;
}

void write_uint32(IWbemClassObject &object, const wchar_t *property,
                  std::uint32_t value) {
  variant property_value;
  // WMI exposes CIM_UINT32 through Automation as VT_I4. Preserve the bits
  // while using the VARIANT type accepted by IWbemClassObject::Put.
  property_value.get().vt = VT_I4;
  property_value.get().lVal = static_cast<LONG>(value);
  check_hresult(object.Put(property, 0, &property_value.get(), CIM_UINT32),
                "IWbemClassObject::Put");
}

std::vector<wchar_t> find_device_interface_path() {
  device_info_set devices{
      SetupDiGetClassDevsW(&kmdf_wmi_ntl_sample::device_interface_guid, nullptr,
                           nullptr, DIGCF_PRESENT | DIGCF_DEVICEINTERFACE)};
  if (!devices)
    return {};

  SP_DEVICE_INTERFACE_DATA interface_data{};
  interface_data.cbSize = sizeof(interface_data);
  if (!SetupDiEnumDeviceInterfaces(devices.get(), nullptr,
                                   &kmdf_wmi_ntl_sample::device_interface_guid,
                                   0, &interface_data))
    return {};

  DWORD required_size = 0;
  (void)SetupDiGetDeviceInterfaceDetailW(devices.get(), &interface_data,
                                         nullptr, 0, &required_size, nullptr);
  if (GetLastError() != ERROR_INSUFFICIENT_BUFFER ||
      required_size < sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_W))
    return {};

  std::vector<std::byte> storage(required_size);
  auto *detail =
      reinterpret_cast<PSP_DEVICE_INTERFACE_DETAIL_DATA_W>(storage.data());
  detail->cbSize = sizeof(*detail);
  if (!SetupDiGetDeviceInterfaceDetailW(devices.get(), &interface_data, detail,
                                        required_size, nullptr, nullptr))
    return {};

  const std::size_t length = std::wcslen(detail->DevicePath);
  return std::vector<wchar_t>(detail->DevicePath,
                              detail->DevicePath + length + 1);
}

com_ptr<IWbemServices> connect_wmi() {
  com_ptr<IWbemLocator> locator;
  check_hresult(CoCreateInstance(CLSID_WbemLocator, nullptr,
                                 CLSCTX_INPROC_SERVER,
                                 IID_PPV_ARGS(locator.put())),
                "CoCreateInstance(CLSID_WbemLocator)");

  com_ptr<IWbemServices> services;
  bstr root_wmi(L"ROOT\\WMI");
  check_hresult(locator->ConnectServer(root_wmi, nullptr, nullptr, nullptr, 0,
                                       nullptr, nullptr, services.put()),
                "IWbemLocator::ConnectServer");
  check_hresult(
      CoSetProxyBlanket(services.get(), RPC_C_AUTHN_WINNT, RPC_C_AUTHZ_NONE,
                        nullptr, RPC_C_AUTHN_LEVEL_CALL,
                        RPC_C_IMP_LEVEL_IMPERSONATE, nullptr, EOAC_NONE),
      "CoSetProxyBlanket");
  return services;
}

com_ptr<IWbemClassObject> query_data_instance(IWbemServices &services) {
  bstr language(L"WQL");
  bstr query(L"SELECT * FROM CrtSys_KmdfWmiSample");

  for (unsigned attempt = 0; attempt != 20; ++attempt) {
    com_ptr<IEnumWbemClassObject> objects;
    const HRESULT query_status = services.ExecQuery(
        language, query, WBEM_FLAG_FORWARD_ONLY | WBEM_FLAG_RETURN_IMMEDIATELY,
        nullptr, objects.put());
    if (SUCCEEDED(query_status)) {
      com_ptr<IWbemClassObject> instance;
      ULONG returned = 0;
      const HRESULT next_status =
          objects->Next(500, 1, instance.put(), &returned);
      if (SUCCEEDED(next_status) && returned == 1)
        return instance;
    } else if (query_status != WBEM_E_INVALID_CLASS) {
      check_hresult(query_status, "IWbemServices::ExecQuery");
    }
    Sleep(250);
  }

  throw std::runtime_error("WMI data instance did not register in time");
}

std::uint32_t execute_transform(IWbemServices &services,
                                IWbemClassObject &instance,
                                std::uint32_t input) {
  com_ptr<IWbemClassObject> data_class;
  bstr class_name(kmdf_wmi_ntl_sample::data_class_name);
  check_hresult(
      services.GetObject(class_name, 0, nullptr, data_class.put(), nullptr),
      "IWbemServices::GetObject");

  com_ptr<IWbemClassObject> input_signature;
  bstr method_name(L"Transform");
  check_hresult(
      data_class->GetMethod(method_name, 0, input_signature.put(), nullptr),
      "IWbemClassObject::GetMethod");

  com_ptr<IWbemClassObject> input_parameters;
  check_hresult(input_signature->SpawnInstance(0, input_parameters.put()),
                "IWbemClassObject::SpawnInstance");
  write_uint32(*input_parameters.get(), L"Input", input);

  const std::wstring object_path = read_string(instance, L"__PATH");
  bstr path(object_path.c_str());
  com_ptr<IWbemClassObject> output_parameters;
  check_hresult(services.ExecMethod(path, method_name, 0, nullptr,
                                    input_parameters.get(),
                                    output_parameters.put(), nullptr),
                "IWbemServices::ExecMethod");
  return read_uint32(*output_parameters.get(), L"Output");
}

com_ptr<IEnumWbemClassObject> subscribe_to_events(IWbemServices &services) {
  bstr language(L"WQL");
  bstr query(L"SELECT * FROM CrtSys_KmdfWmiEvent");
  com_ptr<IEnumWbemClassObject> events;
  check_hresult(services.ExecNotificationQuery(language, query,
                                               WBEM_FLAG_FORWARD_ONLY |
                                                   WBEM_FLAG_RETURN_IMMEDIATELY,
                                               nullptr, events.put()),
                "IWbemServices::ExecNotificationQuery");
  return events;
}

std::uint32_t trigger_event() {
  const auto path = find_device_interface_path();
  if (path.empty())
    throw std::runtime_error("KMDF WMI sample device interface was not found");

  ntl::unique_handle device{
      CreateFileW(path.data(), GENERIC_READ | GENERIC_WRITE,
                  FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING,
                  FILE_ATTRIBUTE_NORMAL, nullptr)};
  if (!device)
    throw std::runtime_error("CreateFileW for the WMI sample failed");

  kmdf_wmi_ntl_sample::trigger_event_reply reply{};
  DWORD bytes_returned = 0;
  if (!DeviceIoControl(device.get(),
                       kmdf_wmi_ntl_sample::trigger_event_ioctl_code, nullptr,
                       0, &reply, sizeof(reply), &bytes_returned, nullptr) ||
      bytes_returned != sizeof(reply))
    throw std::runtime_error("WMI event trigger IOCTL failed");
  return reply.sequence;
}

std::uint32_t wait_for_event(IEnumWbemClassObject &events) {
  com_ptr<IWbemClassObject> event;
  ULONG returned = 0;
  check_hresult(events.Next(5000, 1, event.put(), &returned),
                "IEnumWbemClassObject::Next(event)");
  if (returned != 1)
    throw std::runtime_error("WMI event was not received in time");
  return read_uint32(*event.get(), L"Sequence");
}

} // namespace

int wmain() {
  const HRESULT initialize_status =
      CoInitializeEx(nullptr, COINIT_MULTITHREADED);
  if (FAILED(initialize_status)) {
    std::fprintf(stderr, "CoInitializeEx failed: 0x%08lx\n",
                 static_cast<unsigned long>(initialize_status));
    return 1;
  }
  com_apartment apartment;

  try {
    const HRESULT security_status = CoInitializeSecurity(
        nullptr, -1, nullptr, nullptr, RPC_C_AUTHN_LEVEL_DEFAULT,
        RPC_C_IMP_LEVEL_IMPERSONATE, nullptr, EOAC_NONE, nullptr);
    if (FAILED(security_status) && security_status != RPC_E_TOO_LATE)
      check_hresult(security_status, "CoInitializeSecurity");

    auto services = connect_wmi();
    auto instance = query_data_instance(*services.get());
    const std::uint32_t initial = read_uint32(*instance.get(), L"Value");

    write_uint32(*instance.get(), L"Value", 21);
    check_hresult(services->PutInstance(instance.get(), WBEM_FLAG_UPDATE_ONLY,
                                        nullptr, nullptr),
                  "IWbemServices::PutInstance");

    instance = query_data_instance(*services.get());
    const std::uint32_t updated = read_uint32(*instance.get(), L"Value");
    if (updated != 21)
      throw std::runtime_error("WMI set callback did not update Value");

    const std::uint32_t transformed =
        execute_transform(*services.get(), *instance.get(), 12);
    if (transformed != 24)
      throw std::runtime_error("WMI Transform method returned a wrong value");

    auto events = subscribe_to_events(*services.get());
    Sleep(100);
    const std::uint32_t triggered = trigger_event();
    const std::uint32_t received = wait_for_event(*events.get());
    if (received != triggered)
      throw std::runtime_error("WMI event sequence did not match the IOCTL");

    instance = query_data_instance(*services.get());
    const std::uint32_t queries = read_uint32(*instance.get(), L"QueryCount");
    const std::uint32_t sets = read_uint32(*instance.get(), L"SetCount");
    const std::uint32_t methods = read_uint32(*instance.get(), L"MethodCount");
    const std::uint32_t event_count =
        read_uint32(*instance.get(), L"EventCount");
    if (queries == 0 || sets == 0 || methods == 0 || event_count == 0)
      throw std::runtime_error("WMI callback counters were not updated");

    std::printf("NTL KMDF WMI ok: initial=%u updated=%u transformed=%u "
                "event=%u queries=%u sets=%u methods=%u events=%u\n",
                initial, updated, transformed, received, queries, sets, methods,
                event_count);
    return 0;
  } catch (const std::exception &error) {
    std::fprintf(stderr, "NTL KMDF WMI test failed: %s\n", error.what());
    return 1;
  }
}
