#pragma once

#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>

#include <ntl/net/borrowed_bounded_writer>
#include <ntl/net/inspection/core>
#include <ntl/result>
#include <ntl/status>

namespace crtsys::examples::wfp::content_filter {

// This is the bounded application protocol used by the four content-filter
// examples. It is deliberately transport-neutral: TCP adds its own outer
// message framing while one UDP payload contains exactly one record.
inline constexpr std::uint32_t wire_magic = 0x4e544c52; // "NTLR"
inline constexpr std::uint8_t wire_version = 1;
inline constexpr std::size_t wire_header_size = 16;

enum class classification : std::uint8_t {
  ordinary = 1,
  restricted = 2,
};

class record_view {
public:
  record_view(classification value, std::uint32_t rule_id,
              ntl::net::inspection::content_view body) noexcept
      : classification_(value), rule_id_(rule_id), body_(body) {}

  classification category() const noexcept { return classification_; }
  std::uint32_t rule_id() const noexcept { return rule_id_; }
  ntl::net::inspection::content_view body() const noexcept { return body_; }

private:
  classification classification_ = classification::ordinary;
  std::uint32_t rule_id_ = 0;
  ntl::net::inspection::content_view body_;
};

inline constexpr bool valid(classification value) noexcept {
  return value == classification::ordinary ||
         value == classification::restricted;
}

inline constexpr std::size_t wire_size(std::size_t body_size) noexcept {
  return wire_header_size + body_size;
}

inline ntl::result<record_view>
parse(ntl::net::inspection::content_view input,
      std::size_t maximum_body_size) noexcept {
  if (!input || input.size() < wire_header_size)
    return ntl::unexpected(STATUS_DATA_ERROR);

  ntl::net::borrowed_byte_cursor cursor(input.bytes());
  const auto magic = cursor.read_be32();
  const auto version = cursor.read_u8();
  const auto category = cursor.read_u8();
  const auto flags = cursor.read_be16();
  const auto rule_id = cursor.read_be32();
  const auto encoded_body_size = cursor.read_be32();
  if (!magic || !version || !category || !flags || !rule_id ||
      !encoded_body_size)
    return ntl::unexpected(STATUS_DATA_ERROR);

  const auto parsed_category = static_cast<classification>(*category);
  const std::size_t body_size =
      static_cast<std::size_t>(*encoded_body_size);
  if (*magic != wire_magic || *version != wire_version || *flags != 0 ||
      *rule_id == 0 || !valid(parsed_category) ||
      body_size > maximum_body_size ||
      body_size != input.size() - wire_header_size)
    return ntl::unexpected(STATUS_DATA_ERROR);

  auto body = input.subview(wire_header_size, body_size);
  if (!body)
    return ntl::unexpected(body.status());
  return ntl::ok(record_view(parsed_category, *rule_id, *body));
}

/** Common user/kernel policy for one completely framed sample record. */
inline ntl::net::inspection::verdict
decide(ntl::net::inspection::content_view input,
       std::size_t maximum_body_size) noexcept {
  const auto record = parse(input, maximum_body_size);
  if (!record)
    return ntl::net::inspection::verdict::drop_flow;
  return record->category() == classification::restricted
             ? ntl::net::inspection::verdict::block
             : ntl::net::inspection::verdict::permit;
}

inline ntl::status encode(std::span<std::byte> destination,
                          classification category, std::uint32_t rule_id,
                          std::span<const std::byte> body,
                          std::size_t maximum_body_size) noexcept {
  if (!valid(category) || rule_id == 0 || body.size() > maximum_body_size ||
      body.size() >
          static_cast<std::size_t>(
              (std::numeric_limits<std::uint32_t>::max)()) ||
      destination.size() != wire_size(body.size()))
    return STATUS_INVALID_PARAMETER;

  ntl::net::borrowed_bounded_writer writer(destination);
  ntl::status status = writer.write_be32(wire_magic);
  if (status.is_ok())
    status = writer.write_u8(wire_version);
  if (status.is_ok())
    status = writer.write_u8(static_cast<std::uint8_t>(category));
  if (status.is_ok())
    status = writer.write_be16(0);
  if (status.is_ok())
    status = writer.write_be32(rule_id);
  if (status.is_ok())
    status = writer.write_be32(static_cast<std::uint32_t>(body.size()));
  if (status.is_ok())
    status = writer.append(body);
  if (!status.is_ok())
    return status;
  return writer.size() == destination.size() ? ntl::status::ok()
                                              : ntl::status{STATUS_DATA_ERROR};
}

} // namespace crtsys::examples::wfp::content_filter
