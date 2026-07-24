#pragma once

namespace ntl {
class status;
namespace flt {
class driver;
}
} // namespace ntl

namespace crtsys_minifilter_communication_sample {
ntl::status add_communication_port(ntl::flt::driver &driver);
} // namespace crtsys_minifilter_communication_sample
