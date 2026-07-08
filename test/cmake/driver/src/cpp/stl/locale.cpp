#define _SILENCE_CXX17_CODECVT_HEADER_DEPRECATION_WARNING

//
// https://en.cppreference.com/w/cpp/locale/locale#Example
//
#include <codecvt>
#include <cstddef>
#include <ctime>
#include <iomanip>
#include <iostream>
#include <locale>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace locale_test {
void run() {
  const std::locale previous_global = std::locale();
  const std::locale previous_wcout_locale = std::wcout.getloc();

  std::wcout << L"User-preferred locale setting is "
             << std::locale("").name().c_str() << L'\n';
  // on startup, the global locale is the "C" locale
  std::wcout << 1000.01 << L'\n';
  // replace the C++ global locale and the "C" locale with the user-preferred locale
  std::locale::global(std::locale(""));
  // use the new global locale for future wide character output
  std::wcout.imbue(std::locale());

  // output the same number again
  std::wcout << 1000.01 << L'\n';

  std::locale::global(previous_global);
  std::wcout.imbue(previous_wcout_locale);
}
} // namespace locale_test

//
// https://en.cppreference.com/w/cpp/locale/locale/locale#Example
//
namespace locale_constructor_test {
std::ostream &operator<<(std::ostream &os, const std::locale &loc) {
  if (loc.name().length() <= 80) {
    os << loc.name();
  } else {
    for (const auto c : loc.name()) {
      os << c << (c == ';' ? "\n  " : "");
    }
  }

  return os << '\n';
}

void run() {
  std::locale l1;
  std::cout << "Name of a copy of the classic \"C\" locale: " << l1;
  std::locale l2("en_US.UTF-8");
  std::cout << "Name of unicode locale: " << l2;

  std::locale l3(l1, new std::codecvt_utf8<wchar_t>);
  std::cout << "Name of \"C\" locale except for codecvt: " << l3;

  std::locale l4(l1, l2, std::locale::ctype);
  std::cout << "Name of \"C\" locale except for ctype, which is unicode:\n  "
            << l4;
}
} // namespace locale_constructor_test

//
// https://en.cppreference.com/w/cpp/locale/has_facet#Example
//
namespace has_facet_test {
// minimal custom facet
struct myfacet : public std::locale::facet {
  static std::locale::id id;
};

std::locale::id myfacet::id;

void run() {
  const std::ios_base::fmtflags previous_cout_flags = std::cout.flags();

  // loc is a "C" locale with myfacet added
  std::locale loc(std::locale::classic(), new myfacet);
  std::cout << std::boolalpha << "Can loc classify chars? "
            << std::has_facet<std::ctype<char>>(loc) << '\n'
            << "Can loc classify char32_t? "
            << std::has_facet<std::ctype<char32_t>>(loc) << '\n'
            << "Does loc implement myfacet? " << std::has_facet<myfacet>(loc)
            << '\n';

  std::cout.flags(previous_cout_flags);
}
} // namespace has_facet_test

//
// https://en.cppreference.com/w/cpp/locale/use_facet#Example
//
namespace use_facet_test {
void run() {
  for (const char *name : {"en_US.UTF-8", "de_DE.UTF-8", "en_GB.UTF-8"}) {
    std::cout << "Your currency string is "
              << std::use_facet<std::moneypunct<char, true>>(
                     std::locale{name})
                     .curr_symbol()
              << '\n';
  }
}
} // namespace use_facet_test

//
// https://en.cppreference.com/w/cpp/locale/numpunct#Example
//
namespace numpunct_test {
struct french_bool : std::numpunct<char> {
  string_type do_truename() const override { return "vrai"; }
  string_type do_falsename() const override { return "faux"; }
};

void run() {
  const std::locale previous_cout_locale = std::cout.getloc();
  const std::ios_base::fmtflags previous_cout_flags = std::cout.flags();

  std::cout << "default locale: " << std::boolalpha << true << ", " << false
            << '\n';
  std::cout.imbue(std::locale(std::cout.getloc(), new french_bool));
  std::cout << "locale with modified numpunct: " << std::boolalpha << true
            << ", " << false << '\n';

  std::cout.imbue(previous_cout_locale);
  std::cout.flags(previous_cout_flags);
}
} // namespace numpunct_test

//
// https://en.cppreference.com/w/cpp/locale/ctype_char#Example
//
namespace ctype_char_test {
// This ctype facet classifies commas and endlines as whitespace
struct csv_whitespace : std::ctype<char> {
  static const mask *make_table() {
    // make a copy of the "C" locale table
    static std::vector<mask> v(classic_table(), classic_table() + table_size);
    v[','] |= space;  // comma will be classified as whitespace
    v[' '] &= ~space; // space will not be classified as whitespace
    return &v[0];
  }
  csv_whitespace(std::size_t refs = 0) : ctype(make_table(), false, refs) {}
};

void run() {
  std::string in = "Column 1,Column 2,Column 3\n 123,456,789";
  std::string token;

  std::cout << "Default locale:\n";
  std::istringstream s1(in);
  while (s1 >> token) {
    std::cout << "  " << token << '\n';
  }
  std::cout << "Locale with modified ctype:\n";
  std::istringstream s2(in);
  s2.imbue(std::locale(s2.getloc(), new csv_whitespace));
  while (s2 >> token) {
    std::cout << "  " << token << '\n';
  }
}
} // namespace ctype_char_test

//
// https://en.cppreference.com/w/cpp/locale/messages/open#Example
//
namespace messages_test {
void run() {
  std::locale loc("de_DE.UTF-8");
  const std::messages<char> &facet = std::use_facet<std::messages<char>>(loc);
  const std::messages_base::catalog cat = facet.open("sed", loc);

  if (cat < 0) {
    std::cout << "Could not open german \"sed\" message catalog\n";
    return;
  }

  std::cout << "\"No match\" "
            << facet.get(cat, 0, 0, "No match") << '\n';
  facet.close(cat);
}
} // namespace messages_test

//
// https://en.cppreference.com/w/cpp/io/manip/get_money#Example
//
namespace money_get_test {
void run() {
  long double v1, v2;
  std::string v3;
  std::istringstream in("$1,234.56 2.22 USD  3.33");
  in.imbue(std::locale("en_US.UTF-8"));
  in >> std::get_money(v1) >> std::get_money(v2) >>
      std::get_money(v3, true);

  if (in) {
    std::cout << v1 << ' ' << v2 << ' ' << v3 << '\n';
  } else {
    std::cout << "Parse failed\n";
  }
}
} // namespace money_get_test

//
// https://en.cppreference.com/w/cpp/io/manip/put_money#Example
//
namespace money_put_test {
void run() {
  long double mon = 123.45;
  std::ostringstream out;
  out.imbue(std::locale("en_US.UTF-8"));
  out << std::showbase << "en_US: " << std::put_money(mon) << " or "
      << std::put_money(mon, true) << '\n';
  std::cout << out.str();
}
} // namespace money_put_test

//
// https://en.cppreference.com/w/cpp/io/manip/get_time#Example
//
namespace time_get_test {
void run() {
  std::tm t = {};
  std::istringstream ss("2011-Februar-18 23:12:34");
  ss.imbue(std::locale("de_DE.UTF-8"));
  ss >> std::get_time(&t, "%Y-%b-%d %H:%M:%S");

  if (ss.fail()) {
    std::cout << "Parse failed\n";
  } else {
    std::cout << std::put_time(&t, "%c") << '\n';
  }
}
} // namespace time_get_test

//
// https://en.cppreference.com/w/cpp/io/manip/put_time#Example
//
namespace time_put_test {
void run() {
  std::tm t = {};
  t.tm_year = 2011 - 1900;
  t.tm_mon = 1;
  t.tm_mday = 18;
  t.tm_hour = 23;
  t.tm_min = 12;
  t.tm_sec = 34;

  std::cout << std::put_time(&t, "%c") << '\n';
  std::cout << std::put_time(&t, "%Y-%m-%d %H:%M:%S") << '\n';
}
} // namespace time_put_test

namespace locale_nls_semantic_test {
namespace {
void expect(bool condition, const char *message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}
} // namespace

void run() {
  const std::locale en_us("en_US.UTF-8");

  const auto &ctype = std::use_facet<std::ctype<char>>(en_us);
  expect(ctype.is(std::ctype_base::alpha, 'A'),
         "en_US ctype did not classify A as alpha");
  expect(ctype.is(std::ctype_base::digit, '7'),
         "en_US ctype did not classify 7 as digit");
  expect(ctype.toupper('a') == 'A', "en_US ctype toupper mismatch");
  expect(ctype.tolower('Z') == 'z', "en_US ctype tolower mismatch");

  const auto &collate = std::use_facet<std::collate<char>>(en_us);
  expect(collate.compare("abc", "abc" + 3, "abd", "abd" + 3) < 0,
         "en_US collate ordering mismatch");

  const auto &punct = std::use_facet<std::numpunct<char>>(en_us);
  expect(punct.decimal_point() == '.', "en_US numpunct decimal mismatch");
  expect(!punct.grouping().empty(), "en_US numpunct grouping was empty");

  const std::locale de_de("de_DE.UTF-8");
  const auto &money = std::use_facet<std::moneypunct<char, true>>(de_de);
  expect(!money.curr_symbol().empty(),
         "de_DE moneypunct international currency symbol was empty");

  std::cout << "locale/NLS semantic assertions passed\n";
}
} // namespace locale_nls_semantic_test
