#pragma once

#include <cstddef>
#include <cstdint>
#include <new>
#include <span>
#include <utility>
#include <vector>

#include <ntl/net/framing>
#include <ntl/net/http/datagram>
#include <ntl/status>

namespace crtsys::wfp_kernel_browser_https::driver {

/** Bounded Capsule Protocol byte-stream assembler for one CONNECT stream. */
class webtransport_capsule_stream {
public:
  static constexpr std::size_t maximum_payload_size = 4096;
  static constexpr std::size_t maximum_frame_size =
      maximum_payload_size + 16;
  static constexpr std::size_t maximum_buffered_bytes = 256 * 1024;

  template <class Consumer>
  ntl::status consume(ntl::net::scatter_view input,
                      Consumer &&consumer) noexcept {
    if (input.size() == 0)
      return ntl::status::ok();
    if (!input)
      return STATUS_INVALID_PARAMETER;
    try {
      compact();
      if (input.size() > maximum_buffered_bytes - storage_.size())
        return STATUS_BUFFER_OVERFLOW;
      const std::size_t offset = storage_.size();
      storage_.resize(offset + input.size());
      if (input.size() != 0 &&
          !input.copy_to(std::span<std::byte>(storage_).subspan(offset))
               .is_ok()) {
        storage_.resize(offset);
        return STATUS_DATA_ERROR;
      }

      const ntl::net::http::capsule_framer framer(
          {.maximum_payload_size = maximum_payload_size});
      for (;;) {
        const auto available =
            std::span<const std::byte>(storage_).subspan(consumed_);
        if (available.empty())
          break;
        const auto view =
            ntl::net::scatter_view::from_contiguous(available);
        const auto probe = ntl::net::framing::validate(
            framer.probe(view), available.size(),
            {.maximum_frame_size = maximum_frame_size});
        if (probe.state() == ntl::net::framing::probe_state::need_more)
          break;
        if (probe.state() == ntl::net::framing::probe_state::malformed)
          return probe.error();
        auto frame = view.subview(0, probe.frame_size());
        if (!frame)
          return frame.status();
        auto capsule = ntl::net::http::capsule_view::parse(
            *frame, {.maximum_payload_size = maximum_payload_size});
        if (!capsule)
          return capsule.status();
        const ntl::status accepted = consumer(*capsule);
        if (!accepted.is_ok())
          return accepted;
        consumed_ += probe.frame_size();
        ++capsules_;
      }
      compact();
      return ntl::status::ok();
    } catch (const std::bad_alloc &) {
      return STATUS_INSUFFICIENT_RESOURCES;
    } catch (...) {
      return STATUS_UNHANDLED_EXCEPTION;
    }
  }

  ntl::status finish() noexcept {
    compact();
    return storage_.empty() ? ntl::status::ok()
                            : ntl::status{STATUS_END_OF_FILE};
  }

  std::size_t pending_bytes() const noexcept {
    return storage_.size() - consumed_;
  }

  std::uint64_t capsule_count() const noexcept { return capsules_; }

private:
  void compact() {
    if (consumed_ == 0)
      return;
    if (consumed_ == storage_.size()) {
      storage_.clear();
      consumed_ = 0;
      return;
    }
    storage_.erase(storage_.begin(),
                   storage_.begin() +
                       static_cast<std::ptrdiff_t>(consumed_));
    consumed_ = 0;
  }

  std::vector<std::byte> storage_;
  std::size_t consumed_ = 0;
  std::uint64_t capsules_ = 0;
};

} // namespace crtsys::wfp_kernel_browser_https::driver
