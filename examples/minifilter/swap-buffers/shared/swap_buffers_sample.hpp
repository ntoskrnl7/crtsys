#pragma once

#include <cstdint>
#include <string_view>

namespace crtsys_minifilter_swap_buffers_sample {
inline constexpr std::wstring_view protected_extension = L"ntlxor";
inline constexpr std::wstring_view protected_file_name =
    L"crtsys_minifilter_swap_buffers_sample.ntlxor";
inline constexpr std::wstring_view passthrough_file_name =
    L"crtsys_minifilter_swap_buffers_sample.tmp";
inline constexpr std::uint8_t xor_key = 0xA7;
} // namespace crtsys_minifilter_swap_buffers_sample
