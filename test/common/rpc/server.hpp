#pragma once

#include <ntl/rpc/server>

#include "declare.hpp"

std::shared_ptr<ntl::rpc::server> init_rpc(ntl::driver &driver) {
  driver.on_device_control([](uint32_t ctl_code, const uint8_t *in_buffer,
                              size_t in_buffer_length, uint8_t *out_buffer,
                              size_t *out_buffer_length) {
#include "procedures.cpp"
  });

  return std::make_shared<ntl::rpc::server>(driver, NTL_RPC_NAME);
}