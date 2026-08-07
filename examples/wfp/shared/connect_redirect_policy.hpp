#pragma once

#include <cstdint>
#include <string>
#include <utility>

#include <ntl/wfp/management>

namespace crtsys::examples::wfp::connect_redirect {

/** Builds the identical fail-closed TCP redirect rule for either runtime. */
template <class Layer>
ntl::wfp::connect_redirect_filter_builder<Layer> make_filter(
    ntl::wfp::filter_key<Layer> key, std::wstring name,
    std::uint32_t proxy_process_id, std::uint16_t proxy_port,
    std::uint16_t original_port) {
  ntl::wfp::connect_redirect_filter_builder<Layer> filter(
      key, std::move(name),
      {proxy_process_id, proxy_port,
       ntl::wfp::original_destination_context::preserve},
      ntl::wfp::callout_unavailable::block);
  filter.description(
            L"Preserve the original destination and fail closed when the "
            L"redirect callout is unavailable")
      .protocol_equal(IPPROTO_TCP)
      .remote_port_equal(original_port);
  return filter;
}

} // namespace crtsys::examples::wfp::connect_redirect
