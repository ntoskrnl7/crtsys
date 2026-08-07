#include <ntl/net/http/async_transform>
#include <ntl/net/http/stream_transform>

#include <coroutine>
#include <cstddef>
#include <future>
#include <iostream>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace {

template <class T> class blocking_task {
public:
  struct promise_type {
    blocking_task get_return_object() {
      return blocking_task(result.get_future());
    }
    std::suspend_never initial_suspend() const noexcept { return {}; }
    std::suspend_never final_suspend() const noexcept { return {}; }
    template <class U> void return_value(U &&value) {
      result.set_value(std::forward<U>(value));
    }
    void unhandled_exception() noexcept {
      result.set_exception(std::current_exception());
    }
    std::promise<T> result;
  };

  blocking_task(blocking_task &&) noexcept = default;
  blocking_task &operator=(blocking_task &&) noexcept = default;
  blocking_task(const blocking_task &) = delete;
  blocking_task &operator=(const blocking_task &) = delete;
  T get() { return result_.get(); }

private:
  explicit blocking_task(std::future<T> value) noexcept
      : result_(std::move(value)) {}
  std::future<T> result_;
};

blocking_task<ntl::net::http::pipeline_outcome> apply(
    ntl::net::http::async_transform_runtime &pipeline,
    ntl::net::http::request_message &message) {
  co_return co_await pipeline.apply_borrowed(message);
}

bool async_load() {
  constexpr std::size_t operation_count = 4096;
  ntl::net::http::async_transform_options options;
  options.maximum_concurrency = 8;
  options.maximum_queue_depth = operation_count;
  options.timeout = std::chrono::seconds(10);
  ntl::net::http::async_transform_policy_builder builder({}, options);
  builder.requests().transform(
      [](ntl::net::http::request_message &message,
         const ntl::net::http::async_policy_context &context) {
        if (context.cancellation_requested())
          return ntl::net::http::rewrite_result::block();
        message.headers.set("x-load-policy", message.path);
        return ntl::net::http::rewrite_result::headers_changed();
      });
  auto pipeline = ntl::net::http::async_transform_runtime::create(
      std::move(builder).build());

  std::vector<ntl::net::http::request_message> messages;
  messages.reserve(operation_count);
  for (std::size_t index = 0; index != operation_count; ++index) {
    ntl::net::http::request_message message;
    message.wire_protocol = static_cast<ntl::net::http::protocol>(
        1 + index % 3);
    message.method = "GET";
    message.scheme = "https";
    message.authority = "load.example";
    message.path = "/" + std::to_string(index);
    messages.push_back(std::move(message));
  }

  std::vector<blocking_task<ntl::net::http::pipeline_outcome>> tasks;
  tasks.reserve(operation_count);
  for (auto &message : messages)
    tasks.push_back(apply(pipeline, message));
  for (std::size_t index = 0; index != operation_count; ++index) {
    const auto outcome = tasks[index].get();
    if (outcome.action != ntl::net::http::rewrite_action::forward ||
        !outcome.headers_modified ||
        messages[index].headers.first("x-load-policy") !=
            messages[index].path) {
      const auto statistics = pipeline.statistics();
      std::cerr << "async load mismatch at operation " << index
                << ": action=" << static_cast<unsigned>(outcome.action)
                << ", headers_modified=" << outcome.headers_modified
                << ", completed=" << statistics.completed
                << ", cancelled=" << statistics.cancelled
                << ", timed_out=" << statistics.timed_out
                << ", overloaded=" << statistics.overloaded << '\n';
      return false;
    }
  }
  const auto statistics = pipeline.statistics();
  const bool valid = statistics.submitted == operation_count &&
                     statistics.completed == operation_count &&
                     statistics.cancelled == 0 &&
                     statistics.timed_out == 0 &&
                     statistics.overloaded == 0 &&
                     statistics.queued == 0 && statistics.running == 0;
  if (!valid)
    std::cerr << "async load statistics mismatch: submitted="
              << statistics.submitted << ", completed="
              << statistics.completed << ", cancelled="
              << statistics.cancelled << ", timed_out="
              << statistics.timed_out << ", overloaded="
              << statistics.overloaded << ", queued=" << statistics.queued
              << ", running=" << statistics.running << '\n';
  return valid;
}

bool streaming_load() {
  constexpr std::size_t chunk_size = 64 * 1024;
  constexpr std::size_t chunk_count = 1024;
  ntl::net::http::stream_transform_pipeline pipeline;
  pipeline.chunks().inspect(
      [](const ntl::net::http::stream_message_context_view &,
         const ntl::net::http::stream_chunk_view &) {
        return ntl::net::inspection::verdict::permit;
      });
  ntl::net::http::request_message request;
  request.wire_protocol = ntl::net::http::protocol::http3;
  request.method = "POST";
  request.scheme = "https";
  request.authority = "load.example";
  request.path = "/upload";
  auto opened = pipeline.open(request);
  if (!opened)
    return false;
  auto session = std::move(*opened);
  const std::vector<std::byte> chunk(chunk_size, std::byte{0x5a});
  for (std::size_t index = 0; index != chunk_count; ++index) {
    const auto outcome = session.consume(
        std::span<const std::byte>(chunk), index + 1 == chunk_count);
    if (outcome.action !=
            ntl::net::http::stream_rewrite_action::forward ||
        outcome.bytes != chunk) {
      std::cerr << "streaming load mismatch at chunk " << index
                << ": action=" << static_cast<unsigned>(outcome.action)
                << ", output_bytes=" << outcome.bytes.size() << '\n';
      return false;
    }
  }
  const std::uint64_t expected =
      static_cast<std::uint64_t>(chunk_size) * chunk_count;
  const bool valid = session.finished() && session.input_bytes() == expected &&
                     session.output_bytes() == expected;
  if (!valid)
    std::cerr << "streaming load statistics mismatch: finished="
              << session.finished() << ", input_bytes="
              << session.input_bytes() << ", output_bytes="
              << session.output_bytes() << ", expected=" << expected << '\n';
  return valid;
}

} // namespace

int main() {
  if (!async_load() || !streaming_load()) {
    std::cerr << "NTL policy load contracts failed\n";
    return 1;
  }
  std::cout << "NTL policy load contracts passed: 4096 async messages, "
               "64 MiB streaming body\n";
  return 0;
}
