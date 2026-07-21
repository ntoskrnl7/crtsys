#pragma once

#include <cstdint>

namespace ntl {
namespace flt {
class communication_client;
}
} // namespace ntl

namespace crtsys_minifilter_sample {
bool run_communication_sample();
std::uint32_t
run_coroutine_communication_sample(ntl::flt::communication_client &client);
std::uint32_t
run_coroutine_notification_sample(ntl::flt::communication_client &client);
std::uint32_t
run_coroutine_stream_sample(ntl::flt::communication_client &client);
} // namespace crtsys_minifilter_sample
