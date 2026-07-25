#pragma once

#include <cstddef>
#include <cstdint>

namespace crtsys_flt_name_changer_runtime_test::record_validation {

struct bounded_name {
  std::size_t offset = 0;
  std::size_t size_bytes = 0;
};

struct linked_name_record_layout {
  std::size_t next_offset = 0;
  std::size_t name_length_offset = 0;
  std::size_t name_offset = 0;
  std::size_t record_alignment = 1;
};

struct usn_name_record_layout {
  std::uint16_t major_version = 0;
  std::size_t minimum_size = 0;
  std::size_t name_length_offset = 0;
  std::size_t name_offset_offset = 0;
};

constexpr bool contains_field(std::size_t available, std::size_t offset,
                              std::size_t field_size) noexcept {
  return offset <= available && field_size <= available - offset;
}

inline std::uint16_t load_u16(const unsigned char *field) noexcept {
  return static_cast<std::uint16_t>(
      static_cast<std::uint16_t>(field[0]) |
      (static_cast<std::uint16_t>(field[1]) << 8u));
}

inline std::uint32_t load_u32(const unsigned char *field) noexcept {
  return static_cast<std::uint32_t>(
      static_cast<std::uint32_t>(field[0]) |
      (static_cast<std::uint32_t>(field[1]) << 8u) |
      (static_cast<std::uint32_t>(field[2]) << 16u) |
      (static_cast<std::uint32_t>(field[3]) << 24u));
}

inline void store_u16(unsigned char *field, std::uint16_t value) noexcept {
  field[0] = static_cast<unsigned char>(value & 0xffu);
  field[1] = static_cast<unsigned char>((value >> 8u) & 0xffu);
}

inline void store_u32(unsigned char *field, std::uint32_t value) noexcept {
  field[0] = static_cast<unsigned char>(value & 0xffu);
  field[1] = static_cast<unsigned char>((value >> 8u) & 0xffu);
  field[2] = static_cast<unsigned char>((value >> 16u) & 0xffu);
  field[3] = static_cast<unsigned char>((value >> 24u) & 0xffu);
}

inline bool try_read_counted_name(const unsigned char *record,
                                  std::size_t record_size,
                                  std::size_t name_length_offset,
                                  std::size_t name_offset,
                                  bounded_name &name) noexcept {
  name = {};
  if (!record ||
      !contains_field(record_size, name_length_offset, sizeof(std::uint32_t)) ||
      name_offset > record_size)
    return false;

  const std::size_t name_size = load_u32(record + name_length_offset);
  if ((name_size % sizeof(wchar_t)) != 0 ||
      name_size > record_size - name_offset)
    return false;

  name = {name_offset, name_size};
  return true;
}

inline bool
validate_linked_name_record_chain(const unsigned char *buffer,
                                  std::size_t size_bytes,
                                  linked_name_record_layout layout) noexcept {
  if (size_bytes == 0)
    return true;
  if (!buffer || layout.record_alignment == 0 ||
      !contains_field(layout.name_offset, layout.next_offset,
                      sizeof(std::uint32_t)) ||
      !contains_field(layout.name_offset, layout.name_length_offset,
                      sizeof(std::uint32_t)))
    return false;

  std::size_t offset = 0;
  for (;;) {
    const std::size_t remaining = size_bytes - offset;
    if (remaining < layout.name_offset)
      return false;

    const unsigned char *const record = buffer + offset;
    const std::size_t next = load_u32(record + layout.next_offset);
    const std::size_t record_size = next == 0 ? remaining : next;
    if (record_size < layout.name_offset || record_size > remaining)
      return false;

    bounded_name name;
    if (!try_read_counted_name(record, record_size,
                               layout.name_length_offset, layout.name_offset,
                               name))
      return false;

    if (next == 0)
      return true;
    if ((next % layout.record_alignment) != 0 || next >= remaining)
      return false;
    offset += next;
  }
}

inline bool try_read_usn_name(const unsigned char *buffer,
                              std::size_t available,
                              usn_name_record_layout layout,
                              bounded_name &name) noexcept {
  name = {};
  constexpr std::size_t record_length_offset = 0;
  constexpr std::size_t major_version_offset = sizeof(std::uint32_t);
  if (!buffer || layout.minimum_size == 0 ||
      !contains_field(available, record_length_offset,
                      sizeof(std::uint32_t)) ||
      !contains_field(available, major_version_offset,
                      sizeof(std::uint16_t)))
    return false;

  const std::size_t record_size = load_u32(buffer + record_length_offset);
  const std::uint16_t major = load_u16(buffer + major_version_offset);
  if (major != layout.major_version || record_size < layout.minimum_size ||
      record_size > available ||
      !contains_field(record_size, layout.name_length_offset,
                      sizeof(std::uint16_t)) ||
      !contains_field(record_size, layout.name_offset_offset,
                      sizeof(std::uint16_t)))
    return false;

  const std::size_t name_size = load_u16(buffer + layout.name_length_offset);
  const std::size_t name_offset = load_u16(buffer + layout.name_offset_offset);
  if ((name_size % sizeof(wchar_t)) != 0 || name_offset < layout.minimum_size ||
      name_offset > record_size || name_size > record_size - name_offset)
    return false;

  name = {name_offset, name_size};
  return true;
}

} // namespace crtsys_flt_name_changer_runtime_test::record_validation
