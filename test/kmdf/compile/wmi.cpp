#include <ntl/kmdf/all>

#include <cstdint>

namespace ntl_kmdf_wmi_compile_test {

inline constexpr GUID provider_guid = {
    0x7cba85dd,
    0x6b8e,
    0x4d56,
    {0x9d, 0xa2, 0x5d, 0x3f, 0xe4, 0xe8, 0x50, 0x3c}};

struct data_block {
  std::uint32_t value;
};

struct instance_state {
  std::uint32_t value;
};

constexpr auto provider_control =
    +[](ntl::kmdf::wmi_provider, WDF_WMI_PROVIDER_CONTROL,
        bool) noexcept -> NTSTATUS { return STATUS_SUCCESS; };

constexpr auto query =
    +[](ntl::kmdf::wmi_instance,
        ntl::kmdf::wmi_output_buffer output) noexcept -> NTSTATUS {
  return output.try_write(data_block{});
};

constexpr auto set =
    +[](ntl::kmdf::wmi_instance,
        ntl::kmdf::wmi_input_buffer input) noexcept -> NTSTATUS {
  const auto data = input.try_read<data_block>();
  return data ? STATUS_SUCCESS : static_cast<NTSTATUS>(data.status());
};

constexpr auto set_item =
    +[](ntl::kmdf::wmi_instance, ULONG,
        ntl::kmdf::wmi_input_buffer input) noexcept -> NTSTATUS {
  const auto data = input.try_read<std::uint32_t>();
  return data ? STATUS_SUCCESS : static_cast<NTSTATUS>(data.status());
};

constexpr auto execute =
    +[](ntl::kmdf::wmi_instance, ULONG,
        ntl::kmdf::wmi_method_buffer buffer) noexcept -> NTSTATUS {
  const auto input = buffer.input().try_read<data_block>();
  if (!input)
    return input.status();
  return buffer.output().try_write(*input.value());
};

[[maybe_unused]] void compile_wmi(ntl::kmdf::device device) {
  ntl::kmdf::wmi_provider_config provider(provider_guid);
  provider.minimum_instance_buffer_size(sizeof(data_block))
      .expensive()
      .on_function_control<provider_control>();

  const auto created_provider =
      ntl::kmdf::wmi_provider::try_create(device, provider);
  if (!created_provider)
    return;

  ntl::kmdf::wmi_instance_config instance(created_provider.value());
  instance.register_automatically()
      .on_query<query>()
      .on_set<set>()
      .on_set_item<set_item>()
      .on_execute<execute>();

  ntl::kmdf::object_attributes attributes;
  (void)ntl::kmdf::wmi_instance::try_create<instance_state>(
      device, instance, &attributes);
}

} // namespace ntl_kmdf_wmi_compile_test
