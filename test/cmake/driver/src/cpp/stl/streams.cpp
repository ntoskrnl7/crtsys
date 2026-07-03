//
// https://en.cppreference.com/w/cpp/io/manip/quoted#Example
//
#include <cassert>
#include <iomanip>
#include <iostream>
#include <span>
#if __has_include(<spanstream>)
#include <spanstream>
#endif
#include <sstream>
#include <string>

namespace quoted_test {
void default_delimiter() {
  const std::string in =
      "std::quoted() quotes this string and embedded \"quotes\" too";
  std::stringstream ss;
  ss << std::quoted(in);
  std::string out;
  ss >> std::quoted(out);
  std::cout << "Default delimiter case:\n"
               "read in     ["
            << in << "]\n"
            << "stored as   [" << ss.str() << "]\n"
            << "written out [" << out << "]\n\n";
}

void custom_delimiter() {
  const char delim{'$'};
  const char escape{'%'};
  const std::string in =
      "std::quoted() quotes this string and embedded $quotes$ $too";
  std::stringstream ss;
  ss << std::quoted(in, delim, escape);
  std::string out;
  ss >> std::quoted(out, delim, escape);

  std::cout << "Custom delimiter case:\n"
               "read in     ["
            << in << "]\n"
            << "stored as   [" << ss.str() << "]\n"
            << "written out [" << out << "]\n\n";
}

void run() {
  default_delimiter();
  custom_delimiter();
}
} // namespace quoted_test

//
// https://en.cppreference.com/w/cpp/io/basic_spanstream/span#Example
//
namespace spanstream_test {
void run() {
#if defined(__cpp_lib_spanstream) && __cpp_lib_spanstream >= 202106L
  char out_buf[16];
  std::ospanstream ost{std::span<char>{out_buf}};
  ost << "C++" << ' ' << 23 << '\0'; // note explicit null-termination
  auto sp = ost.span();
  assert(sp[0] == 'C' && sp[1] == '+' && sp[2] == '+' && sp[3] == ' ' &&
         sp[4] == '2' && sp[5] == '3' && sp[6] == '\0');
  std::cout << "sp.data(): [" << sp.data() << "]\n";
  std::cout << "out_buf: [" << out_buf << "]\n";
  // spanstream uses out_buf as internal storage, no allocations
  assert(static_cast<char *>(out_buf) == sp.data());
  const char in_buf[] = "X Y 42";
  std::ispanstream ist{std::span<const char>{in_buf}};
  assert(static_cast<const char *>(in_buf) == ist.span().data());
  char c;
  ist >> c;
  assert(c == 'X');
  ist >> c;
  assert(c == 'Y');
  int i;
  ist >> i;
  assert(i == 42);
  ist >> i; // buffer is exhausted
  assert(!ist);
  // Release builds compile out assert(), so mark assert-only locals as used.
  (void)c;
  (void)i;
#else
  std::cout << "std::spanstream is not available in this MSVC STL\n";
#endif
}
} // namespace spanstream_test
