#include "inspection_policy.hpp"

#include "tls_inspection_policy.hpp"

namespace crtsys::wfp_sample::tls_inspection {

ntl::net::http::inspection_policy make_inspection_policy() {
  return crtsys::examples::wfp::tls_inspection::make_inspection_policy();
}

} // namespace crtsys::wfp_sample::tls_inspection
