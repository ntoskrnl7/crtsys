#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <windows.h>

#include "http2_inspection.hpp"

#include <array>
#include <charconv>
#include <coroutine>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <exception>
#include <filesystem>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include <ntl/net/http2/framing>
#include <ntl/net/http2/hpack>
#include <ntl/net/http2/transform>
#include <ntl/net/inspection/core>
#include <ntl/net/tls/framed_stream>

#include "bidirectional_relay.hpp"
#include "http1_support.hpp"

namespace crtsys::wfp_sample::browser_https {
namespace {

class http2_request_sink final
    : public ntl::net::http2::inspection_sink {
public:
  ntl::status on_headers(
      std::uint32_t stream_id,
      std::span<const ntl::net::http2::header_field> fields,
      bool end_stream) noexcept override {
    try {
      const auto found = streams_.find(stream_id);
      if (found != streams_.end()) {
        if (!end_stream)
          return STATUS_DATA_ERROR;
        for (const auto &field : fields) {
          if (field.name.empty() || field.name.front() == ':')
            return STATUS_DATA_ERROR;
          if (!valid_regular_header(field))
            return STATUS_DATA_ERROR;
        }
        streams_.erase(found);
        return ntl::status::ok();
      }
      if (streams_.size() >= maximum_tracked_streams)
        return STATUS_QUOTA_EXCEEDED;

      bool regular_seen = false;
      bool method_seen = false;
      bool scheme_seen = false;
      bool authority_seen = false;
      bool path_seen = false;
      bool connect = false;
      std::optional<std::size_t> content_length;
      for (const auto &field : fields) {
        if (field.name.empty())
          return STATUS_DATA_ERROR;
        for (const unsigned char character : field.name) {
          if (character >= 'A' && character <= 'Z')
            return STATUS_DATA_ERROR;
        }
        const bool pseudo = field.name.front() == ':';
        if (pseudo && regular_seen)
          return STATUS_DATA_ERROR;
        regular_seen = regular_seen || !pseudo;
        if (!pseudo) {
          if (!valid_regular_header(field))
            return STATUS_DATA_ERROR;
          if (field.name == "content-length") {
            std::size_t parsed = 0;
            const auto converted = std::from_chars(
                field.value.data(),
                field.value.data() + field.value.size(),
                parsed);
            if (converted.ec != std::errc{} ||
                converted.ptr !=
                    field.value.data() + field.value.size() ||
                (content_length && *content_length != parsed))
              return STATUS_DATA_ERROR;
            content_length = parsed;
          }
          continue;
        }

        if (field.name == ":method" && !method_seen) {
          method_seen = true;
          connect = field.value == "CONNECT";
        } else if (field.name == ":scheme" && !scheme_seen) {
          scheme_seen = true;
        } else if (field.name == ":authority" &&
                   !authority_seen) {
          authority_seen = true;
        } else if (field.name == ":path" && !path_seen) {
          path_seen = true;
        } else {
          // Extended CONNECT (:protocol) is a separate product path.
          return STATUS_NOT_SUPPORTED;
        }
      }

      if (!method_seen || !authority_seen ||
          (connect ? (scheme_seen || path_seen)
                   : (!scheme_seen || !path_seen)))
        return STATUS_DATA_ERROR;
      if (end_stream) {
        if (content_length && *content_length != 0)
          return STATUS_DATA_ERROR;
        return ntl::status::ok();
      }
      streams_.emplace(
          stream_id,
          request_state{content_length, 0});
      return ntl::status::ok();
    } catch (...) {
      return STATUS_INSUFFICIENT_RESOURCES;
    }
  }

  ntl::status on_data(
      std::uint32_t stream_id, ntl::net::scatter_view data,
      bool end_stream) noexcept override {
    const auto found = streams_.find(stream_id);
    if (found == streams_.end())
      return STATUS_DATA_ERROR;
    auto &stream = found->second;
    if (data.size() > maximum_http_body_size - stream.received)
      return STATUS_BUFFER_OVERFLOW;
    stream.received += data.size();
    if (!end_stream)
      return ntl::status::ok();
    if (stream.content_length &&
        *stream.content_length != stream.received)
      return STATUS_DATA_ERROR;
    streams_.erase(found);
    return ntl::status::ok();
  }

private:
  struct request_state {
    std::optional<std::size_t> content_length;
    std::size_t received = 0;
  };

  static bool valid_regular_header(
      const ntl::net::http2::header_field &field) noexcept {
    if (field.name == "connection" ||
        field.name == "proxy-connection" ||
        field.name == "keep-alive" ||
        field.name == "transfer-encoding" ||
        field.name == "upgrade")
      return false;
    return field.name != "te" ||
           ascii_equal_ci(
               trim_http_ows(field.value), "trailers");
  }

  static constexpr std::size_t maximum_tracked_streams = 256;
  std::unordered_map<std::uint32_t, request_state> streams_;
};

class http2_response_sink final
    : public ntl::net::http2::inspection_sink {
public:
  http2_response_sink(
      std::wstring server_name,
      const ntl::net::inspection::content_decoder_registry &decoders,
      browser_html_logger &logger) noexcept
      : server_name_(std::move(server_name)),
        decoders_(&decoders), logger_(&logger) {}

  ntl::status on_headers(
      std::uint32_t stream_id,
      std::span<const ntl::net::http2::header_field> fields,
      bool end_stream) noexcept override {
    try {
      auto found = streams_.find(stream_id);
      if (found == streams_.end()) {
        if (streams_.size() >= maximum_tracked_streams)
          return STATUS_QUOTA_EXCEEDED;
        found = streams_.try_emplace(stream_id).first;
      }
      auto &stream = found->second;
      if (stream.final_headers) {
        if (!end_stream)
          return STATUS_DATA_ERROR;
        for (const auto &field : fields) {
          if (field.name.empty() || field.name.front() == ':')
            return STATUS_DATA_ERROR;
        }
        return complete(stream_id);
      }

      bool regular_seen = false;
      bool status_seen = false;
      unsigned status = 0;
      std::string content_type;
      std::string content_encoding;
      std::optional<std::size_t> content_length;
      for (const auto &field : fields) {
        if (field.name.empty())
          return STATUS_DATA_ERROR;
        for (const unsigned char character : field.name) {
          if (character >= 'A' && character <= 'Z')
            return STATUS_DATA_ERROR;
        }
        const bool pseudo = field.name.front() == ':';
        if (pseudo && regular_seen)
          return STATUS_DATA_ERROR;
        regular_seen = regular_seen || !pseudo;
        if (pseudo) {
          if (field.name != ":status" || status_seen ||
              field.value.size() != 3)
            return STATUS_DATA_ERROR;
          const auto converted = std::from_chars(
              field.value.data(),
              field.value.data() + field.value.size(),
              status);
          if (converted.ec != std::errc{} ||
              converted.ptr !=
                  field.value.data() + field.value.size() ||
              status < 100 || status > 999)
            return STATUS_DATA_ERROR;
          status_seen = true;
          continue;
        }
        if (field.name == "connection" ||
            field.name == "proxy-connection" ||
            field.name == "keep-alive" ||
            field.name == "transfer-encoding" ||
            field.name == "upgrade")
          return STATUS_DATA_ERROR;
        if (field.name == "content-type")
          content_type = field.value;
        else if (field.name == "content-encoding") {
          if (!content_encoding.empty())
            content_encoding.append(", ");
          content_encoding.append(field.value);
        } else if (field.name == "content-length") {
          std::size_t parsed = 0;
          const auto converted = std::from_chars(
              field.value.data(),
              field.value.data() + field.value.size(),
              parsed);
          if (converted.ec != std::errc{} ||
              converted.ptr !=
                  field.value.data() + field.value.size() ||
              (content_length && *content_length != parsed))
            return STATUS_DATA_ERROR;
          content_length = parsed;
        }
      }
      if (!status_seen)
        return STATUS_DATA_ERROR;
      if (status >= 100 && status < 200) {
        if (end_stream)
          return STATUS_DATA_ERROR;
        return ntl::status::ok();
      }

      stream.status = status;
      stream.content_type = std::move(content_type);
      stream.content_encoding = std::move(content_encoding);
      stream.content_length = content_length;
      stream.final_headers = true;
      return end_stream ? complete(stream_id)
                        : ntl::status::ok();
    } catch (...) {
      return STATUS_INSUFFICIENT_RESOURCES;
    }
  }

  ntl::status on_data(
      std::uint32_t stream_id, ntl::net::scatter_view data,
      bool end_stream) noexcept override {
    try {
      const auto found = streams_.find(stream_id);
      if (found == streams_.end() ||
          !found->second.final_headers)
        return STATUS_DATA_ERROR;
      auto &body = found->second.body;
      if (data.size() > maximum_http_body_size - body.size()) {
        logger_->record_error(
            "HTTP/2 encoded body limit host=" +
            narrow_dns_name(server_name_) +
            " stream=" + std::to_string(stream_id) +
            " retained=" + std::to_string(body.size()) +
            " incoming=" + std::to_string(data.size()) +
            " maximum=" +
            std::to_string(maximum_http_body_size));
        return STATUS_BUFFER_OVERFLOW;
      }
      if (data.size() >
          maximum_buffered_bodies - buffered_body_bytes_) {
        logger_->record_error(
            "HTTP/2 connection body quota host=" +
            narrow_dns_name(server_name_) +
            " stream=" + std::to_string(stream_id) +
            " retained=" +
            std::to_string(buffered_body_bytes_) +
            " incoming=" + std::to_string(data.size()) +
            " maximum=" +
            std::to_string(maximum_buffered_bodies));
        return STATUS_QUOTA_EXCEEDED;
      }
      const auto copied = data.for_each_chunk(
          [&body](std::span<const std::byte> chunk) noexcept {
            try {
              body.insert(
                  body.end(), chunk.begin(), chunk.end());
              return true;
            } catch (...) {
              return false;
            }
          });
      if (!copied.is_ok())
        return STATUS_INSUFFICIENT_RESOURCES;
      buffered_body_bytes_ += data.size();
      return end_stream ? complete(stream_id)
                        : ntl::status::ok();
    } catch (...) {
      return STATUS_INSUFFICIENT_RESOURCES;
    }
  }

  unsigned last_status() const noexcept {
    return last_status_;
  }

  const std::optional<std::filesystem::path> &
  html_path() const noexcept {
    return html_path_;
  }

private:
  struct stream_state {
    unsigned status = 0;
    std::string content_type;
    std::string content_encoding;
    std::optional<std::size_t> content_length;
    std::vector<std::byte> body;
    bool final_headers = false;
  };

  ntl::status complete(std::uint32_t stream_id) noexcept {
    try {
      const auto found = streams_.find(stream_id);
      if (found == streams_.end() ||
          !found->second.final_headers)
        return STATUS_DATA_ERROR;
      auto &stream = found->second;
      if (stream.content_length &&
          *stream.content_length != stream.body.size())
        return STATUS_DATA_ERROR;
      const std::size_t buffered_size = stream.body.size();
      const auto encoded =
          ntl::net::scatter_view::from_contiguous(
              std::span<const std::byte>(stream.body));
      auto decoded = ntl::net::inspection::decode_content_encoding(
          *decoders_, encoded, stream.content_encoding,
          {.maximum_encoded_size = maximum_http_body_size,
           .maximum_decoded_size = maximum_http_body_size,
           .maximum_expansion_ratio = 64,
           .maximum_coding_layers = 4});
      if (!decoded) {
        logger_->record_error(
            "HTTP/2 content decode failed host=" +
            narrow_dns_name(server_name_) +
            " stream=" + std::to_string(stream_id) +
            " encoding=" +
            (stream.content_encoding.empty()
                 ? std::string("identity")
                 : stream.content_encoding) +
            " status=" +
            std::to_string(static_cast<std::uint32_t>(
                static_cast<NTSTATUS>(decoded.status()))));
        return decoded.status();
      }

      parsed_http_response response;
      response.status = stream.status;
      response.content_type = std::move(stream.content_type);
      response.content_encoding =
          std::move(stream.content_encoding);
      response.body = std::move(*decoded);
      response.wire_size = stream.body.size();
      response.body_decoded = true;
      last_status_ = response.status;
      auto logged =
          logger_->record_response(server_name_, response);
      if (logged && !html_path_)
        html_path_ = std::move(logged);
      streams_.erase(found);
      buffered_body_bytes_ -= buffered_size;
      return ntl::status::ok();
    } catch (...) {
      return STATUS_INSUFFICIENT_RESOURCES;
    }
  }

  std::wstring server_name_;
  const ntl::net::inspection::content_decoder_registry *decoders_;
  browser_html_logger *logger_;
  static constexpr std::size_t maximum_tracked_streams = 256;
  static constexpr std::size_t maximum_buffered_bodies =
      16 * 1024 * 1024;
  std::unordered_map<std::uint32_t, stream_state> streams_;
  std::size_t buffered_body_bytes_ = 0;
  unsigned last_status_ = 0;
  std::optional<std::filesystem::path> html_path_;
};

class serialized_tls_writer {
private:
  struct operation {
    explicit operation(std::vector<std::byte> value)
        : bytes(std::move(value)) {}
    std::vector<std::byte> bytes;
    std::coroutine_handle<> continuation{};
    std::exception_ptr failure;
    std::size_t transferred = 0;
  };

  class detached_pump {
  public:
    struct promise_type {
      detached_pump get_return_object() noexcept {
        return detached_pump(
            std::coroutine_handle<promise_type>::
                from_promise(*this));
      }
      std::suspend_always initial_suspend() const noexcept {
        return {};
      }
      std::suspend_never final_suspend() const noexcept {
        return {};
      }
      void return_void() const noexcept {}
      void unhandled_exception() const noexcept {
        std::terminate();
      }
    };

    explicit detached_pump(
        std::coroutine_handle<promise_type> handle) noexcept
        : handle_(handle) {}
    detached_pump(const detached_pump &) = delete;
    detached_pump &operator=(const detached_pump &) = delete;
    detached_pump(detached_pump &&other) noexcept
        : handle_(std::exchange(other.handle_, {})) {}
    ~detached_pump() {
      if (handle_)
        handle_.destroy();
    }
    std::coroutine_handle<> release() noexcept {
      return std::exchange(handle_, {});
    }

  private:
    std::coroutine_handle<promise_type> handle_{};
  };

public:
  explicit serialized_tls_writer(
      ntl::net::tls_stream &stream) noexcept
      : stream_(&stream) {}

  serialized_tls_writer(
      const serialized_tls_writer &) = delete;
  serialized_tls_writer &
  operator=(const serialized_tls_writer &) = delete;

  class write_awaitable {
  public:
    write_awaitable(
        serialized_tls_writer &owner,
        std::vector<std::byte> bytes)
        : owner_(&owner),
          operation_(std::make_shared<operation>(
              std::move(bytes))) {}

    bool await_ready() const noexcept { return false; }

    std::coroutine_handle<>
    await_suspend(std::coroutine_handle<> continuation) {
      operation_->continuation = continuation;
      return owner_->enqueue(operation_);
    }

    std::size_t await_resume() {
      if (operation_->failure)
        std::rethrow_exception(operation_->failure);
      return operation_->transferred;
    }

  private:
    serialized_tls_writer *owner_;
    std::shared_ptr<operation> operation_;
  };

  write_awaitable write(std::vector<std::byte> bytes) {
    return write_awaitable(*this, std::move(bytes));
  }

private:
  std::coroutine_handle<> enqueue(
      std::shared_ptr<operation> next) {
    bool start = false;
    {
      std::lock_guard lock(lock_);
      if (failure_) {
        next->failure = failure_;
        return next->continuation;
      }
      queue_.push_back(std::move(next));
      if (!active_) {
        active_ = true;
        start = true;
      }
    }
    if (!start)
      return std::noop_coroutine();
    auto task = pump();
    return task.release();
  }

  detached_pump pump() noexcept {
    for (;;) {
      std::shared_ptr<operation> current;
      {
        std::lock_guard lock(lock_);
        if (queue_.empty()) {
          active_ = false;
          co_return;
        }
        current = std::move(queue_.front());
        queue_.pop_front();
      }

      try {
        current->transferred =
            co_await stream_->write_all(current->bytes);
        if (current->transferred != current->bytes.size())
          throw std::runtime_error(
              "serialized TLS write completed short");
      } catch (...) {
        const auto failure = std::current_exception();
        current->failure = failure;
        std::deque<std::shared_ptr<operation>> abandoned;
        {
          std::lock_guard lock(lock_);
          failure_ = failure;
          abandoned.swap(queue_);
          active_ = false;
        }
        for (auto &operation : abandoned) {
          operation->failure = failure;
          operation->continuation.resume();
        }
        current->continuation.resume();
        co_return;
      }
      bool idle = false;
      {
        std::lock_guard lock(lock_);
        if (queue_.empty()) {
          active_ = false;
          idle = true;
        }
      }
      current->continuation.resume();
      if (idle)
        co_return;
    }
  }

  ntl::net::tls_stream *stream_;
  std::mutex lock_;
  std::deque<std::shared_ptr<operation>> queue_;
  std::exception_ptr failure_;
  bool active_ = false;
};

class http2_send_window {
public:
  class reservation {
  public:
    reservation(
        http2_send_window &owner,
        std::uint32_t stream_id,
        std::size_t bytes) noexcept
        : owner_(&owner),
          stream_id_(stream_id),
          bytes_(bytes) {}

    reservation(const reservation &) = delete;
    reservation &operator=(const reservation &) = delete;

    bool await_ready() {
      std::lock_guard lock(owner_->lock_);
      return owner_->reserve_locked(stream_id_, bytes_);
    }

    bool await_suspend(std::coroutine_handle<> continuation) {
      std::lock_guard lock(owner_->lock_);
      if (owner_->reserve_locked(stream_id_, bytes_))
        return false;
      if (owner_->cancelled_ ||
          bytes_ >
              static_cast<std::size_t>(0x7fffffffu) ||
          owner_->closed_streams_.contains(stream_id_)) {
        cancelled_ = true;
        return false;
      }
      continuation_ = continuation;
      owner_->waiters_.push_back(this);
      return true;
    }

    void await_resume() {
      if (cancelled_)
        throw std::runtime_error(
            "HTTP/2 send window cancelled");
    }

  private:
    friend class http2_send_window;
    http2_send_window *owner_;
    std::uint32_t stream_id_;
    std::size_t bytes_;
    std::coroutine_handle<> continuation_{};
    bool cancelled_ = false;
  };

  reservation reserve(
      std::uint32_t stream_id,
      std::size_t bytes) noexcept {
    return reservation(*this, stream_id, bytes);
  }

  bool update(
      std::uint32_t stream_id,
      std::uint32_t increment) noexcept {
    if (increment == 0 || increment > 0x7fffffffu)
      return false;
    std::vector<std::coroutine_handle<>> ready;
    {
      std::lock_guard lock(lock_);
      std::int64_t &window =
          stream_id == 0
              ? connection_window_
              : stream_windows_.try_emplace(
                    stream_id, initial_stream_window_)
                    .first->second;
      if (window >
          static_cast<std::int64_t>(0x7fffffffu - increment))
        return false;
      window += increment;
      wake_locked(ready);
    }
    for (const auto continuation : ready)
      continuation.resume();
    return true;
  }

  bool set_initial(std::uint32_t value) noexcept {
    if (value > 0x7fffffffu)
      return false;
    std::vector<std::coroutine_handle<>> ready;
    {
      std::lock_guard lock(lock_);
      const std::int64_t next = value;
      const std::int64_t delta =
          next - initial_stream_window_;
      if (delta > 0) {
        for (const auto &[stream_id, window] :
             stream_windows_) {
          (void)stream_id;
          if (window >
              static_cast<std::int64_t>(
                  0x7fffffffu) -
                  delta)
            return false;
        }
      }
      for (auto &[stream_id, window] : stream_windows_) {
        (void)stream_id;
        window += delta;
      }
      initial_stream_window_ = next;
      wake_locked(ready);
    }
    for (const auto continuation : ready)
      continuation.resume();
    return true;
  }

  void close(std::uint32_t stream_id) noexcept {
    std::vector<std::coroutine_handle<>> ready;
    {
      std::lock_guard lock(lock_);
      stream_windows_.erase(stream_id);
      closed_streams_.insert(stream_id);
      for (auto current = waiters_.begin();
           current != waiters_.end();) {
        auto *waiter = *current;
        if (waiter->stream_id_ != stream_id) {
          ++current;
          continue;
        }
        waiter->cancelled_ = true;
        ready.push_back(waiter->continuation_);
        current = waiters_.erase(current);
      }
    }
    for (const auto continuation : ready)
      continuation.resume();
  }

  void cancel() noexcept {
    std::vector<std::coroutine_handle<>> ready;
    {
      std::lock_guard lock(lock_);
      if (cancelled_)
        return;
      cancelled_ = true;
      for (auto *waiter : waiters_) {
        waiter->cancelled_ = true;
        ready.push_back(waiter->continuation_);
      }
      waiters_.clear();
    }
    for (const auto continuation : ready)
      continuation.resume();
  }

private:
  bool reserve_locked(
      std::uint32_t stream_id,
      std::size_t bytes) {
    if (cancelled_ ||
        bytes >
            static_cast<std::size_t>(0x7fffffffu) ||
        closed_streams_.contains(stream_id))
      return false;
    auto &stream = stream_windows_.try_emplace(
        stream_id, initial_stream_window_).first->second;
    const auto count = static_cast<std::int64_t>(bytes);
    if (connection_window_ < count || stream < count)
      return false;
    connection_window_ -= count;
    stream -= count;
    return true;
  }

  void wake_locked(
      std::vector<std::coroutine_handle<>> &ready) {
    for (auto current = waiters_.begin();
         current != waiters_.end();) {
      auto *waiter = *current;
      if (!reserve_locked(
              waiter->stream_id_, waiter->bytes_)) {
        ++current;
        continue;
      }
      ready.push_back(waiter->continuation_);
      current = waiters_.erase(current);
    }
  }

  std::mutex lock_;
  std::int64_t connection_window_ = 65535;
  std::int64_t initial_stream_window_ = 65535;
  std::unordered_map<std::uint32_t, std::int64_t>
      stream_windows_;
  std::unordered_set<std::uint32_t> closed_streams_;
  std::vector<reservation *> waiters_;
  bool cancelled_ = false;
};

class http2_preface_barrier {
public:
  class awaitable {
  public:
    explicit awaitable(
        http2_preface_barrier &owner) noexcept
        : owner_(&owner) {}

    bool await_ready() const noexcept {
      std::lock_guard lock(owner_->lock_);
      return owner_->ready_;
    }

    bool await_suspend(std::coroutine_handle<> continuation) {
      std::lock_guard lock(owner_->lock_);
      if (owner_->ready_)
        return false;
      owner_->continuation_ = continuation;
      return true;
    }

    void await_resume() {
      std::lock_guard lock(owner_->lock_);
      if (owner_->failure_)
        std::rethrow_exception(owner_->failure_);
    }

  private:
    http2_preface_barrier *owner_;
  };

  awaitable wait() noexcept { return awaitable(*this); }

  void signal() noexcept {
    complete({});
  }

  void fail(std::exception_ptr failure) noexcept {
    complete(std::move(failure));
  }

private:
  void complete(std::exception_ptr failure) noexcept {
    std::coroutine_handle<> continuation;
    {
      std::lock_guard lock(lock_);
      if (ready_)
        return;
      ready_ = true;
      failure_ = std::move(failure);
      continuation = std::exchange(continuation_, {});
    }
    if (continuation)
      continuation.resume();
  }

  mutable std::mutex lock_;
  bool ready_ = false;
  std::exception_ptr failure_;
  std::coroutine_handle<> continuation_{};
};

class http2_relay_observation {
public:
  void record_status(unsigned status) noexcept {
    std::lock_guard lock(lock_);
    last_status_ = status;
  }

  void record_response(
      unsigned status,
      std::optional<std::filesystem::path> path) noexcept {
    std::lock_guard lock(lock_);
    last_status_ = status;
    if (path && !html_path_)
      html_path_ = std::move(path);
  }

  unsigned last_status() const noexcept {
    std::lock_guard lock(lock_);
    return last_status_;
  }

  std::optional<std::filesystem::path>
  html_path() const {
    std::lock_guard lock(lock_);
    return html_path_;
  }

private:
  mutable std::mutex lock_;
  unsigned last_status_ = 0;
  std::optional<std::filesystem::path> html_path_;
};

std::uint32_t read_u32(
    ntl::net::scatter_view bytes,
    std::size_t offset = 0) {
  std::array<std::byte, 4> value{};
  const ntl::status copied =
      bytes.copy_to(value, offset);
  if (!copied.is_ok())
    throw std::runtime_error(
        "HTTP/2 control frame payload is truncated");
  return
      (static_cast<std::uint32_t>(
           std::to_integer<std::uint8_t>(value[0]))
       << 24) |
      (static_cast<std::uint32_t>(
           std::to_integer<std::uint8_t>(value[1]))
       << 16) |
      (static_cast<std::uint32_t>(
           std::to_integer<std::uint8_t>(value[2]))
       << 8) |
      static_cast<std::uint32_t>(
          std::to_integer<std::uint8_t>(value[3]));
}

void observe_flow_control(
    const ntl::net::http2::frame_view &frame,
    http2_send_window &window) {
  const auto &header = frame.header();
  if (header.type ==
      ntl::net::http2::frame_type::window_update) {
    const std::uint32_t increment =
        read_u32(frame.payload()) & 0x7fffffffu;
    if (!window.update(header.stream_id, increment))
      throw std::runtime_error(
          "invalid HTTP/2 WINDOW_UPDATE");
    return;
  }
  if (header.type !=
          ntl::net::http2::frame_type::settings ||
      header.acknowledgement())
    return;
  for (std::size_t offset = 0;
       offset != header.payload_size; offset += 6) {
    std::array<std::byte, 6> setting{};
    const ntl::status copied =
        frame.payload().copy_to(setting, offset);
    if (!copied.is_ok())
      throw std::runtime_error(
          "HTTP/2 SETTINGS payload is truncated");
    const std::uint16_t identifier =
        static_cast<std::uint16_t>(
            (std::to_integer<std::uint8_t>(setting[0])
             << 8) |
            std::to_integer<std::uint8_t>(setting[1]));
    if (identifier != 0x4u)
      continue;
    const std::uint32_t value =
        (static_cast<std::uint32_t>(
             std::to_integer<std::uint8_t>(setting[2]))
         << 24) |
        (static_cast<std::uint32_t>(
             std::to_integer<std::uint8_t>(setting[3]))
         << 16) |
        (static_cast<std::uint32_t>(
             std::to_integer<std::uint8_t>(setting[4]))
         << 8) |
        static_cast<std::uint32_t>(
            std::to_integer<std::uint8_t>(setting[5]));
    if (!window.set_initial(value))
      throw std::runtime_error(
          "invalid HTTP/2 SETTINGS_INITIAL_WINDOW_SIZE");
  }
}

nested_task<std::size_t> write_http2_frames(
    serialized_tls_writer &writer,
    http2_send_window &window,
    std::vector<ntl::net::http2::outbound_frame> frames) {
  std::size_t transferred = 0;
  for (auto &frame : frames) {
    if (frame.flow_controlled_bytes != 0)
      co_await window.reserve(
          frame.stream_id,
          frame.flow_controlled_bytes);
    const bool end_stream =
        frame.wire.size() >=
            ntl::net::http2::frame_header_size &&
        (std::to_integer<std::uint8_t>(frame.wire[4]) &
         0x01u) != 0 &&
        (std::to_integer<std::uint8_t>(frame.wire[3]) ==
             static_cast<std::uint8_t>(
                 ntl::net::http2::frame_type::data) ||
         std::to_integer<std::uint8_t>(frame.wire[3]) ==
             static_cast<std::uint8_t>(
                 ntl::net::http2::frame_type::headers));
    transferred += co_await writer.write(
        std::move(frame.wire));
    if (end_stream)
      window.close(frame.stream_id);
  }
  co_return transferred;
}

coroutine_task<std::size_t> relay_http2_frames(
    ntl::net::tls_stream &source,
    serialized_tls_writer &destination_writer,
    serialized_tls_writer &source_writer,
    http2_send_window &destination_window,
    http2_send_window &source_window,
    ntl::net::http2::connection_transformer &transformer,
    bool expect_client_preface,
    std::string_view direction,
    std::wstring_view server_name,
    browser_html_logger &logger,
    http2_relay_observation &observation,
    http2_preface_barrier &prefaces) {
  constexpr std::array<std::byte, 24> client_preface{
      std::byte{0x50}, std::byte{0x52}, std::byte{0x49},
      std::byte{0x20}, std::byte{0x2a}, std::byte{0x20},
      std::byte{0x48}, std::byte{0x54}, std::byte{0x54},
      std::byte{0x50}, std::byte{0x2f}, std::byte{0x32},
      std::byte{0x2e}, std::byte{0x30}, std::byte{0x0d},
      std::byte{0x0a}, std::byte{0x0d}, std::byte{0x0a},
      std::byte{0x53}, std::byte{0x4d}, std::byte{0x0d},
      std::byte{0x0a}, std::byte{0x0d}, std::byte{0x0a}};
  try {
    std::size_t relayed = 0;
    if (expect_client_preface) {
      std::array<std::byte, client_preface.size()> received{};
      std::size_t offset = 0;
      while (offset != received.size()) {
        const std::size_t count = co_await source.read_some(
            std::span<std::byte>(received).subspan(offset));
        if (count == 0)
          throw std::runtime_error(
              "HTTP/2 client closed before its connection preface");
        offset += count;
      }
      if (received != client_preface)
        throw std::runtime_error(
            "HTTP/2 client sent an invalid connection preface");
      std::vector<std::byte> preface(
          received.begin(), received.end());
      relayed += co_await destination_writer.write(
          std::move(preface));
    }

    constexpr std::size_t maximum_frame_payload =
        1024 * 1024;
    ntl::net::tls_framed_stream frames(
        source,
        ntl::net::http2::frame_framer(
            {maximum_frame_payload, false}),
        {maximum_frame_payload +
             ntl::net::http2::frame_header_size},
        16 * 1024);
    bool first_frame = true;
    for (;;) {
      auto message = co_await frames.read_frame_or_eof();
      if (!message) {
        destination_window.cancel();
        source_window.cancel();
        co_return relayed;
      }
      const auto wire =
          ntl::net::scatter_view::from_contiguous(
              message->frame());
      const auto frame =
          ntl::net::http2::frame_view::parse(
              wire, {maximum_frame_payload, false});
      if (!frame)
        throw std::runtime_error(
            "HTTP/2 frame failed validation after framing");
      if (first_frame &&
          (frame->header().type !=
               ntl::net::http2::frame_type::settings ||
           frame->header().acknowledgement()))
        throw std::runtime_error(
            "HTTP/2 connection preface did not begin with SETTINGS");

      observe_flow_control(*frame, source_window);
      auto transformed = transformer.consume(*frame);
      if (!transformed) {
        const auto native_status =
            static_cast<std::uint32_t>(
                static_cast<NTSTATUS>(
                    transformed.status()));
        std::array<char, 9> status_text{};
        const auto converted = std::to_chars(
            status_text.data(),
            status_text.data() + status_text.size() - 1,
            native_status, 16);
        throw std::runtime_error(
            "HTTP/2 transform rejected frame direction=" +
            std::string(direction) + " type=" +
            std::to_string(static_cast<unsigned>(
                frame->header().type)) +
            " stream=" +
            std::to_string(frame->header().stream_id) +
            " status=0x" +
            std::string(status_text.data(), converted.ptr));
      }

      if (transformed->received_flow_controlled_bytes != 0) {
        const auto increment = static_cast<std::uint32_t>(
            transformed->received_flow_controlled_bytes);
        auto connection_credit =
            ntl::net::http2::encode_window_update(
                0, increment);
        if (!connection_credit)
          throw std::runtime_error(
              "cannot encode HTTP/2 connection credit");
        std::vector<ntl::net::http2::outbound_frame> credits;
        credits.push_back(std::move(*connection_credit));
        if (!frame->header().end_stream()) {
          auto stream_credit =
              ntl::net::http2::encode_window_update(
                  transformed->stream_id, increment);
          if (!stream_credit)
            throw std::runtime_error(
                "cannot encode HTTP/2 stream credit");
          credits.push_back(std::move(*stream_credit));
        }
        relayed += co_await write_http2_frames(
            source_writer, source_window,
            std::move(credits));
      }

      if (!transformed->consumed) {
        std::vector<std::byte> original(
            message->frame().begin(), message->frame().end());
        relayed += co_await destination_writer.write(
            std::move(original));
        if (first_frame) {
          first_frame = false;
          if (expect_client_preface)
            co_await prefaces.wait();
          else
            prefaces.signal();
        }
        continue;
      }

      relayed += co_await write_http2_frames(
          destination_writer, destination_window,
          std::move(transformed->forward));
      relayed += co_await write_http2_frames(
          source_writer, source_window,
          std::move(transformed->reverse));

      if (transformed->terminal_status != 0)
        observation.record_status(
            transformed->terminal_status);
      if (transformed->response) {
        const auto &response = *transformed->response;
        parsed_http_response inspected;
        inspected.status = response.status;
        inspected.content_type = std::string(
            response.headers.first("content-type")
                .value_or(std::string_view{}));
        inspected.content_encoding = std::string(
            response.headers.first("content-encoding")
                .value_or(std::string_view{}));
        inspected.location = std::string(
            response.headers.first("location")
                .value_or(std::string_view{}));
        inspected.body = response.body;
        inspected.wire_size = response.body.size();
        inspected.body_decoded = true;
        auto logged =
            logger.record_response(server_name, inspected);
        observation.record_response(
            response.status, std::move(logged));
      }
    }
  } catch (...) {
    prefaces.fail(std::current_exception());
    destination_window.cancel();
    source_window.cancel();
    throw;
  }
}

} // namespace

nested_task<browser_proxy_result> relay_http2_connection(
    SOCKET inbound_socket,
    SOCKET outbound_socket,
    ntl::net::tls_stream &inbound,
    ntl::net::tls_stream &outbound,
    std::wstring server_name,
    const ntl::net::inspection::content_decoder_registry &decoders,
    const ntl::net::inspection::content_encoder_registry &encoders,
    const ntl::net::http::transform_pipeline &transforms,
    browser_html_logger &logger) {
  ntl::net::http2::exchange_store exchanges;
  ntl::net::http2::connection_transformer
      request_transformer(
          ntl::net::http2::connection_direction::requests,
          exchanges, transforms, decoders, encoders);
  ntl::net::http2::connection_transformer
      response_transformer(
          ntl::net::http2::connection_direction::responses,
          exchanges, transforms, decoders, encoders);
  serialized_tls_writer inbound_writer(inbound);
  serialized_tls_writer outbound_writer(outbound);
  http2_send_window inbound_window;
  http2_send_window outbound_window;
  http2_relay_observation observation;
  http2_preface_barrier prefaces;
  auto client_to_origin = relay_http2_frames(
      inbound, outbound_writer, inbound_writer,
      outbound_window, inbound_window,
      request_transformer, true, "browser-to-origin",
      server_name, logger, observation, prefaces);
  auto origin_to_client = relay_http2_frames(
      outbound, inbound_writer, outbound_writer,
      inbound_window, outbound_window,
      response_transformer, false, "origin-to-browser",
      server_name, logger, observation, prefaces);
  co_await join_bidirectional_relays(
      std::move(client_to_origin),
      std::move(origin_to_client),
      inbound_socket, outbound_socket);
  co_return browser_proxy_result{
      std::move(server_name), observation.last_status(),
      observation.html_path()};
}

} // namespace crtsys::wfp_sample::browser_https
