#include <ntddk.h>

#include <atomic>
#include <memory>
#include <string>

#include <ntl/driver>
#include <ntl/rpc/server>
#include <ntl/status>

#include "rpc_cross_bitness.hpp"

ntl::status ntl::main(ntl::driver &driver, const std::wstring &) {
  auto echo_count = std::make_shared<std::atomic<std::uint32_t>>(0);
  auto server =
      ntl::rpc::make_server(driver, L"crtsys_rpc_cross_bitness");
  server
      ->on(crtsys_rpc_cross_bitness::architecture, [] {
        return rpc_architecture_info{
            static_cast<std::uint32_t>(sizeof(void *) * 8),
            static_cast<std::uint32_t>(sizeof(std::size_t) * 8),
            static_cast<std::uint32_t>(sizeof(std::ptrdiff_t) * 8),
            static_cast<std::uint32_t>(sizeof(wchar_t) * 8)};
      })
      .on(crtsys_rpc_cross_bitness::echo,
          [echo_count](const rpc_cross_bitness_payload &payload) {
            echo_count->fetch_add(1, std::memory_order_relaxed);
            return payload;
          })
      .on(crtsys_rpc_cross_bitness::narrow_reply,
          [] { return std::uint32_t{0x89ABCDEFu}; })
      .on(crtsys_rpc_cross_bitness::vector_size,
          [](const std::vector<std::uint8_t> &values) {
            return static_cast<std::uint32_t>(values.size());
          })
      .on(crtsys_rpc_cross_bitness::echo_count, [echo_count] {
        return echo_count->load(std::memory_order_relaxed);
      });
  driver.on_unload([server, echo_count]() mutable {
    server.reset();
    echo_count.reset();
  });
  return ntl::status::ok();
}
