#pragma once

#include <ntl/flt/rpc>
#include <ntl/ipc/common>
#include <ntl/ipc/shared_ring>

#include <cstddef>
#include <cstdint>
#include <string_view>

namespace crtsys_minifilter_sample {
inline constexpr std::wstring_view file_name =
    L"crtsys_minifilter_ntl_sample.tmp";
inline constexpr std::wstring_view renamed_file_name =
    L"crtsys_minifilter_ntl_sample_renamed.tmp";
struct shared_record {
  std::uint32_t sequence = 0;
  std::uint32_t value = 0;
};

using shared_record_ring = ntl::ipc::shared_ring<shared_record, 4>;
inline constexpr std::size_t shared_ring_stride =
    (shared_record_ring::required_bytes() +
     shared_record_ring::required_alignment() - 1) /
    shared_record_ring::required_alignment() *
    shared_record_ring::required_alignment();
inline constexpr std::size_t shared_region_bytes = shared_ring_stride * 2;

struct progress_event {
  std::uint32_t percent = 0;

  friend zpp::serializer::access;
  template <typename Archive, typename Self>
  static void serialize(Archive &archive, Self &self) {
    archive(self.percent);
  }
};

inline constexpr ntl::rpc::method<0xA63, std::uint32_t(std::uint32_t)>
    app_transform_method{};
inline constexpr ntl::rpc::method<0xA64, std::uint32_t(std::uint32_t)>
    call_app_method{};

#if !defined(NTL_USER_MODE)
std::uint64_t
publish_progress_callback(const ntl::flt::communication_context &context,
                          std::uint32_t percent, bool reliable);
std::uint32_t call_app_callback(const ntl::flt::communication_context &context,
                                std::uint32_t value);
#endif

} // namespace crtsys_minifilter_sample

NTL_FLT_DECLARE_NOTIFICATION_ID(crtsys_minifilter_sample, 0xB10,
                                crtsys_minifilter_sample::progress_event,
                                progress)
NTL_FLT_DECLARE_STREAM_ID(crtsys_minifilter_sample, 0xB11, numbers,
                          std::uint32_t, std::uint32_t)

NTL_FLT_RPC_BEGIN_CONTRACT(crtsys_minifilter_sample,
                           L"\\CrtSysMinifilterNtlSamplePort", 1, 0x3ull)

NTL_FLT_REGISTER_NOTIFICATION(crtsys_minifilter_sample, progress);

NTL_FLT_REGISTER_STREAM(crtsys_minifilter_sample, numbers, value, stream, {
  auto reply = stream.try_write(value * 2);
  if (!reply)
    throw ntl::exception(reply.status(), "Failed to queue stream reply");
});

NTL_FLT_ADD_CALLBACK_ID(crtsys_minifilter_sample, 0xA60,
                        std::uint32_t(std::uint32_t), ping,
                        [](std::uint32_t value) noexcept { return value + 1; })

NTL_FLT_ADD_CALLBACK_ID(crtsys_minifilter_sample, 0xA62,
                        std::uint64_t(std::uint32_t, bool), publish_progress,
                        crtsys_minifilter_sample::publish_progress_callback)

NTL_FLT_ADD_CALLBACK_ID(
    crtsys_minifilter_sample, 0xA61,
    std::uint32_t(ntl::ipc::buffer_token, ntl::ipc::buffer_token),
    transform_shared_records,
    [](const ntl::flt::communication_context &context,
       ntl::ipc::buffer_token app_to_driver,
       ntl::ipc::buffer_token driver_to_app) -> std::uint32_t {
      auto input = context.try_resolve(app_to_driver,
                                       ntl::ipc::region_access::driver_read);
      if (!input)
        throw ntl::exception(input.status(),
                             "Failed to resolve the input ring");

      auto output = context.try_resolve(driver_to_app,
                                        ntl::ipc::region_access::driver_write);
      if (!output)
        throw ntl::exception(output.status(),
                             "Failed to resolve the output ring");

      crtsys_minifilter_sample::shared_record_ring input_ring;
      crtsys_minifilter_sample::shared_record_ring output_ring;
      if (crtsys_minifilter_sample::shared_record_ring::attach(
              input->data(), input->size(), input_ring) !=
              ntl::ipc::validation_status::success ||
          crtsys_minifilter_sample::shared_record_ring::attach(
              output->data(), output->size(), output_ring) !=
              ntl::ipc::validation_status::success)
        throw ntl::exception(STATUS_INVALID_PARAMETER,
                             "Invalid shared-ring layout");

      std::uint32_t processed = 0;
      crtsys_minifilter_sample::shared_record record{};
      while (input_ring.try_read(record)) {
        record.value += 1000;
        if (!output_ring.try_write(record))
          throw ntl::exception(STATUS_BUFFER_OVERFLOW,
                               "The output ring is full");
        ++processed;
      }
      return processed;
    })

NTL_FLT_RPC_END(crtsys_minifilter_sample)
