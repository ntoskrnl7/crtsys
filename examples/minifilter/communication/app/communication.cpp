#include "communication.hpp"

#include "minifilter_sample.hpp"

#include <Windows.h>
#include <fltUser.h>

#include <cstdint>
#include <exception>
#include <iomanip>
#include <iostream>

namespace crtsys_minifilter_communication_sample {

bool run_communication_sample() {
  try {
    auto client = connect();
    client.on_request(app_transform_method,
                      [](std::uint32_t value) noexcept { return value * 3; });

    const auto ping_reply = ping(client, std::uint32_t{41});
    if (ping_reply != 42) {
      std::cerr << "typed minifilter ping returned " << ping_reply << '\n';
      return false;
    }
    if (client.invoke(call_app_method, std::uint32_t{14}) != 42) {
      std::cerr << "driver-to-app request returned a bad result\n";
      return false;
    }

    auto pending_ping = ping_async(client, std::uint32_t{41});
    if (pending_ping.get() != 42 ||
        run_coroutine_communication_sample(client) != 42) {
      std::cerr << "async minifilter ping returned a bad result\n";
      return false;
    }

    auto progress_wait = progress_async(client);
    (void)publish_progress(client, std::uint32_t{25}, false);
    if (progress_wait.get().percent != 25) {
      std::cerr << "minifilter notification returned a bad value\n";
      return false;
    }
    if (run_coroutine_notification_sample(client) != 50) {
      std::cerr << "coroutine minifilter notification returned a bad value\n";
      return false;
    }

    auto number_stream = numbers(client);
    number_stream.write(std::uint32_t{21});
    const auto doubled = number_stream.read();
    if (!doubled.payload().has_value() || doubled.payload().value() != 42) {
      std::cerr << "minifilter stream returned a bad value\n";
      return false;
    }
    number_stream.acknowledge(doubled);
    number_stream.close();
    if (run_coroutine_stream_sample(client) != 84) {
      std::cerr << "coroutine minifilter stream returned a bad value\n";
      return false;
    }

    auto region = client.register_shared_region(shared_region_bytes);
    auto *const base = static_cast<unsigned char *>(region.data());
    shared_record_ring app_to_driver;
    shared_record_ring driver_to_app;
    if (shared_record_ring::initialize(
            base, shared_record_ring::required_bytes(), app_to_driver, 1) !=
            ntl::ipc::validation_status::success ||
        shared_record_ring::initialize(
            base + shared_ring_stride, shared_record_ring::required_bytes(),
            driver_to_app, 2) != ntl::ipc::validation_status::success) {
      std::cerr << "failed to initialize shared rings\n";
      return false;
    }

    for (std::uint32_t index = 0; index != 2; ++index) {
      if (!app_to_driver.try_write(shared_record{index, index + 10})) {
        std::cerr << "the app-to-driver ring is full\n";
        return false;
      }
    }

    const auto input = region.token(0, shared_record_ring::required_bytes());
    const auto output =
        region.token(shared_ring_stride, shared_record_ring::required_bytes());
    const auto processed = transform_shared_records(client, input, output);
    if (processed != 2) {
      std::cerr << "the driver processed " << processed << " records\n";
      return false;
    }

    for (std::uint32_t index = 0; index != processed; ++index) {
      shared_record record{};
      if (!driver_to_app.try_read(record) || record.sequence != index ||
          record.value != index + 1010) {
        std::cerr << "shared-ring response mismatch\n";
        return false;
      }
    }

    std::cout << "typed port ping: " << ping_reply
              << ", driver-to-app request: 42"
              << ", async/coroutine ping: 42"
              << ", notification: 25/50, stream reply: 42/84"
              << ", shared records: " << processed << '\n';
    return true;
  } catch (const ntl::flt::communication_error &failure) {
    std::cerr << "minifilter communication failed: 0x" << std::hex
              << std::uppercase << static_cast<unsigned long>(failure.result())
              << '\n';
  } catch (const std::exception &failure) {
    std::cerr << "minifilter communication failed: " << failure.what() << '\n';
  }
  return false;
}

} // namespace crtsys_minifilter_communication_sample
