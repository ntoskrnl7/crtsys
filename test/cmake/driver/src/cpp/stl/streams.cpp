//
// https://en.cppreference.com/w/cpp/io/manip/quoted#Example
//
#include <iomanip>
#include <iostream>
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
