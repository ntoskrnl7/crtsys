#pragma once

#include <cstddef>
#include <cstdint>
#include <guiddef.h>
#include <ntl/wfp/layers>

#ifndef CTL_CODE
#error Include <wdm.h> in kernel mode or <winioctl.h> in user mode first.
#endif

namespace wfp_kernel_connect_redirect {

using layer_v4 = ntl::wfp::layers::ale_connect_redirect_v4;
using layer_v6 = ntl::wfp::layers::ale_connect_redirect_v6;

inline constexpr wchar_t service_name[] =
    L"crtsys_wfp_kernel_connect_redirect";
inline constexpr wchar_t device_name[] = L"CrtSysWfpKernelConnectRedirect";
inline constexpr wchar_t user_device_path[] =
    L"\\\\.\\CrtSysWfpKernelConnectRedirect";
inline constexpr std::size_t maximum_message_size = 4096;

struct proxy_info {
  std::uint32_t process_id;
  std::uint16_t port_v4;
  std::uint16_t port_v6;
  std::uint64_t accepted;
  std::uint64_t redirect_records;
  std::uint64_t completed;
  std::uint64_t failed;
  std::uint64_t bytes_to_origin;
  std::uint64_t bytes_to_client;
};

struct query_proxy_contract {
  static constexpr unsigned long device_type = FILE_DEVICE_UNKNOWN;
  static constexpr unsigned long function = 0x96a;
  static constexpr unsigned long method = METHOD_BUFFERED;
  static constexpr unsigned long access = FILE_READ_DATA;
  using input_type = void;
  using output_type = proxy_info;
};
inline constexpr unsigned long query_proxy_ioctl =
    CTL_CODE(query_proxy_contract::device_type, query_proxy_contract::function,
             query_proxy_contract::method, query_proxy_contract::access);

// {F6898636-9F47-4B43-A516-B15D13AC73ED}
inline constexpr GUID device_class_guid{
    0xf6898636, 0x9f47, 0x4b43,
    {0xa5, 0x16, 0xb1, 0x5d, 0x13, 0xac, 0x73, 0xed}};
// {2E6224F9-D2F0-4AC5-A7CF-0EE9F1A64CA6}
inline constexpr ntl::wfp::provider_key provider_key{
    GUID{0x2e6224f9, 0xd2f0, 0x4ac5,
         {0xa7, 0xcf, 0x0e, 0xe9, 0xf1, 0xa6, 0x4c, 0xa6}}};
// {94FB4901-A8EF-4F5D-9167-876B1A0264C2}
inline constexpr ntl::wfp::sublayer_key sublayer_key{
    GUID{0x94fb4901, 0xa8ef, 0x4f5d,
         {0x91, 0x67, 0x87, 0x6b, 0x1a, 0x02, 0x64, 0xc2}}};
// {244B95E1-8A51-472F-8FBA-DF480CD1BE08}
inline constexpr ntl::wfp::terminating_callout_key<layer_v4> callout_key_v4{
    GUID{0x244b95e1, 0x8a51, 0x472f,
         {0x8f, 0xba, 0xdf, 0x48, 0x0c, 0xd1, 0xbe, 0x08}}};
// {DC88B584-CF57-46F7-BD11-6F8C031A34D7}
inline constexpr ntl::wfp::filter_key<layer_v4> filter_key_v4{
    GUID{0xdc88b584, 0xcf57, 0x46f7,
         {0xbd, 0x11, 0x6f, 0x8c, 0x03, 0x1a, 0x34, 0xd7}}};
// {FBE28136-FA8D-404A-B337-38C9204391B0}
inline constexpr ntl::wfp::terminating_callout_key<layer_v6> callout_key_v6{
    GUID{0xfbe28136, 0xfa8d, 0x404a,
         {0xb3, 0x37, 0x38, 0xc9, 0x20, 0x43, 0x91, 0xb0}}};
// {D25E5949-8C1C-48F7-9B11-E67E8DFD0E76}
inline constexpr ntl::wfp::filter_key<layer_v6> filter_key_v6{
    GUID{0xd25e5949, 0x8c1c, 0x48f7,
         {0x9b, 0x11, 0xe6, 0x7e, 0x8d, 0xfd, 0x0e, 0x76}}};

} // namespace wfp_kernel_connect_redirect
