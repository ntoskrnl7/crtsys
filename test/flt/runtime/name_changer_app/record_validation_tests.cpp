#include "record_validation_tests.hpp"

#include "../name_changer_shared/output_record_validation.hpp"

#include <array>
#include <cstddef>
#include <cstdint>

namespace crtsys_flt_name_changer_runtime_test {
namespace {

using record_validation::linked_name_record_layout;
using record_validation::store_u16;
using record_validation::store_u32;
using record_validation::usn_name_record_layout;

constexpr linked_name_record_layout directory_layout{0, 4, 8, 8};
constexpr usn_name_record_layout usn_layout{2, 12, 8, 10};

std::array<unsigned char, 48> valid_directory_chain() {
  std::array<unsigned char, 48> bytes{};
  store_u32(bytes.data(), 24);
  store_u32(bytes.data() + 4, 4);
  bytes[8] = 'a';
  bytes[10] = 'b';

  store_u32(bytes.data() + 24, 0);
  store_u32(bytes.data() + 28, 4);
  bytes[32] = 'c';
  bytes[34] = 'd';
  return bytes;
}

std::array<unsigned char, 32> valid_usn_record() {
  std::array<unsigned char, 32> bytes{};
  store_u32(bytes.data(), static_cast<std::uint32_t>(bytes.size()));
  store_u16(bytes.data() + 4, 2);
  store_u16(bytes.data() + 8, 4);
  store_u16(bytes.data() + 10, 16);
  bytes[16] = 'n';
  bytes[18] = 'm';
  return bytes;
}

bool require(bool condition, const char *message, std::string &failure) {
  if (condition)
    return true;
  failure = message;
  return false;
}

} // namespace

bool run_record_validation_tests(std::string &failure) {
  using record_validation::bounded_name;
  using record_validation::try_read_usn_name;
  using record_validation::validate_linked_name_record_chain;

  auto directory = valid_directory_chain();
  if (!require(validate_linked_name_record_chain(
                   directory.data(), directory.size(), directory_layout),
               "a valid two-record directory chain was rejected", failure))
    return false;

  if (!require(!validate_linked_name_record_chain(
                   directory.data(), 7, directory_layout),
               "a truncated directory header was accepted", failure))
    return false;

  directory = valid_directory_chain();
  store_u32(directory.data() + 4, 3);
  if (!require(!validate_linked_name_record_chain(
                   directory.data(), directory.size(), directory_layout),
               "an odd directory name length was accepted", failure))
    return false;

  directory = valid_directory_chain();
  store_u32(directory.data(), 20);
  if (!require(!validate_linked_name_record_chain(
                   directory.data(), directory.size(), directory_layout),
               "an unaligned directory continuation was accepted", failure))
    return false;

  directory = valid_directory_chain();
  store_u32(directory.data(), 4);
  if (!require(!validate_linked_name_record_chain(
                   directory.data(), directory.size(), directory_layout),
               "a directory continuation inside its header was accepted",
               failure))
    return false;

  directory = valid_directory_chain();
  store_u32(directory.data(), static_cast<std::uint32_t>(directory.size()));
  if (!require(!validate_linked_name_record_chain(
                   directory.data(), directory.size(), directory_layout),
               "a directory chain without a terminal record was accepted",
               failure))
    return false;

  directory = valid_directory_chain();
  store_u32(directory.data() + 4, 18);
  if (!require(!validate_linked_name_record_chain(
                   directory.data(), directory.size(), directory_layout),
               "a directory name extending beyond its record was accepted",
               failure))
    return false;

  auto usn = valid_usn_record();
  bounded_name name;
  if (!require(try_read_usn_name(usn.data(), usn.size(), usn_layout, name) &&
                   name.offset == 16 && name.size_bytes == 4,
               "a valid USN V2-style record was rejected", failure))
    return false;

  if (!require(!try_read_usn_name(usn.data(), usn.size() - 1, usn_layout,
                                  name),
               "a USN record longer than the returned buffer was accepted",
               failure))
    return false;

  usn = valid_usn_record();
  store_u16(usn.data() + 4, 3);
  if (!require(!try_read_usn_name(usn.data(), usn.size(), usn_layout, name),
               "a mismatched USN major version was accepted", failure))
    return false;

  usn = valid_usn_record();
  store_u16(usn.data() + 8, 3);
  if (!require(!try_read_usn_name(usn.data(), usn.size(), usn_layout, name),
               "an odd USN name length was accepted", failure))
    return false;

  usn = valid_usn_record();
  store_u16(usn.data() + 10, 33);
  if (!require(!try_read_usn_name(usn.data(), usn.size(), usn_layout, name),
               "a USN name offset beyond its record was accepted", failure))
    return false;

  usn = valid_usn_record();
  store_u16(usn.data() + 8, 18);
  if (!require(!try_read_usn_name(usn.data(), usn.size(), usn_layout, name),
               "a USN name extending beyond its record was accepted",
               failure))
    return false;

  usn = valid_usn_record();
  store_u32(usn.data(), 10);
  if (!require(!try_read_usn_name(usn.data(), usn.size(), usn_layout, name),
               "a USN record shorter than its fixed header was accepted",
               failure))
    return false;

  return true;
}

} // namespace crtsys_flt_name_changer_runtime_test
