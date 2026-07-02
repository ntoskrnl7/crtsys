//
// https://en.cppreference.com/w/cpp/regex#Example
//
#include <algorithm>
#include <cstddef>
#include <iostream>
#include <iterator>
#include <regex>
#include <string>

namespace regex_test {
void run() {
  std::string s = "Some people, when confronted with a problem, think "
                  "\"I know, I'll use regular expressions.\" "
                  "Now they have two problems.";
  std::regex self_regex(
      "REGULAR EXPRESSIONS",
      std::regex_constants::ECMAScript | std::regex_constants::icase);
  if (std::regex_search(s, self_regex)) {
    std::cout << "Text contains the phrase 'regular expressions'\n";
  }

  std::regex word_regex("(\\w+)");
  auto words_begin = std::sregex_iterator(s.begin(), s.end(), word_regex);
  auto words_end = std::sregex_iterator();
  std::cout << "Found " << std::distance(words_begin, words_end) << " words\n";

  const int N = 6;
  std::cout << "Words longer than " << N << " characters:\n";
  for (std::sregex_iterator i = words_begin; i != words_end; ++i) {
    std::smatch match = *i;
    std::string match_str = match.str();
    if (match_str.size() > N) {
      std::cout << "  " << match_str << '\n';
    }
  }
  std::regex long_word_regex("(\\w{7,})");
  std::string new_s = std::regex_replace(s, long_word_regex, "[$&]");
  std::cout << new_s << '\n';
}
} // namespace regex_test

//
// https://en.cppreference.com/w/cpp/regex/regex_match#Example
//
namespace regex_match_test {
void run() {
  const char *fnames[] = {"foo.txt", "bar.txt", "test", "a0.txt", "AAA.txt"};
  const std::regex txt_regex("[a-z]+\\.txt");

  for (const auto fname : fnames) {
    std::cout << fname << ": " << std::regex_match(fname, txt_regex) << '\n';
  }

  const std::regex base_regex("([a-z]+)\\.txt");
  std::smatch base_match;

  for (const auto fname : fnames) {
    const std::string fname_str = fname;
    if (std::regex_match(fname_str, base_match, base_regex)) {
      if (base_match.size() == 2) {
        std::ssub_match base_sub_match = base_match[1];
        const std::string base = base_sub_match.str();
        std::cout << fname << " has a base of " << base << '\n';
      }
    }
  }

  std::regex pieces_regex("([a-z]+)\\.([a-z]+)");
  std::cmatch pieces_match;

  if (std::regex_match("foo.txt", pieces_match, pieces_regex)) {
    std::cout << '\n';
    for (std::size_t i = 0; i < pieces_match.size(); ++i) {
      // pieces_match is a cmatch, so its sub_match type is csub_match. Keep
      // the C-string match path while avoiding a mismatched sub_match type.
      std::csub_match sub_match = pieces_match[i];
      const std::string piece = sub_match.str();
      std::cout << "  submatch " << i << ": " << piece << '\n';
    }
  }
}
} // namespace regex_match_test

//
// https://en.cppreference.com/w/cpp/regex/regex_iterator#Example
//
namespace regex_iterator_test {
void run() {
  const std::string s = "Quick brown fox.";
  const std::regex words_regex("[^\\s]+");

  auto words_begin = std::sregex_iterator(s.begin(), s.end(), words_regex);
  auto words_end = std::sregex_iterator();

  std::cout << "Found " << std::distance(words_begin, words_end) << " words:\n";

  for (std::sregex_iterator i = words_begin; i != words_end; ++i) {
    std::smatch match = *i;
    std::string match_str = match.str();
    std::cout << match_str << '\n';
  }
}
} // namespace regex_iterator_test

//
// https://en.cppreference.com/w/cpp/regex/regex_token_iterator#Example
//
namespace regex_token_iterator_test {
void run() {
  std::string text = "Quick brown fox.";
  std::regex ws_re("\\s+"); // whitespace
  std::copy(std::sregex_token_iterator(text.begin(), text.end(), ws_re, -1),
            std::sregex_token_iterator(),
            std::ostream_iterator<std::string>(std::cout, "\n"));

  std::string html =
      "<p><a href=\"http://google.com\">google</a> "
      "< a HREF =\"http://cppreference.com\">cppreference</a>\n</p>";
  std::regex url_re(R"!!(<\s*A\s+[^>]*href\s*=\s*"([^"]*)")!!",
                    std::regex::icase);
  std::copy(std::sregex_token_iterator(html.begin(), html.end(), url_re, 1),
            std::sregex_token_iterator(),
            std::ostream_iterator<std::string>(std::cout, "\n"));
}
} // namespace regex_token_iterator_test
