//
// https://en.cppreference.com/w/cpp/regex#Example
//
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
  // Simple regular expression matching
  const std::string fnames[] = {"foo.txt", "bar.txt", "baz.dat", "zoidberg"};
  const std::regex txt_regex("[a-z]+\\.txt");

  for (const auto &fname : fnames) {
    std::cout << fname << ": " << std::regex_match(fname, txt_regex) << '\n';
  }
  // Extraction of a sub-match
  const std::regex base_regex("([a-z]+)\\.txt");
  std::smatch base_match;
  for (const auto &fname : fnames) {
    if (std::regex_match(fname, base_match, base_regex)) {
      // The first sub_match is the whole string; the next
      // sub_match is the first parenthesized expression.
      if (base_match.size() == 2) {
        std::ssub_match base_sub_match = base_match[1];
        std::string base = base_sub_match.str();
        std::cout << fname << " has a base of " << base << '\n';
      }
    }
  }
  // Extraction of several sub-matches
  const std::regex pieces_regex("([a-z]+)\\.([a-z]+)");
  std::smatch pieces_match;
  for (const auto &fname : fnames) {
    if (std::regex_match(fname, pieces_match, pieces_regex)) {
      std::cout << fname << '\n';
      for (std::size_t i = 0; i < pieces_match.size(); ++i) {
        std::ssub_match sub_match = pieces_match[i];
        std::string piece = sub_match.str();
        std::cout << "  submatch " << i << ": " << piece << '\n';
      }
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

  std::regex words_regex("[^\\s]+");
  auto words_begin = std::sregex_iterator(s.begin(), s.end(), words_regex);
  auto words_end = std::sregex_iterator();

  std::cout << "Found " << std::distance(words_begin, words_end)
            << " words:\n";
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
  // The cppreference page includes <fstream>, but this example does not use
  // file streams; the driver harness keeps the unused hosted header out.
  // Tokenization (non-matched fragments)
  // Note that regex is matched only two times; when the third value is obtained
  // the iterator is a suffix iterator.
  const std::string text = "Quick brown fox.";
  const std::regex ws_re("\\s+"); // whitespace
  std::copy(std::sregex_token_iterator(text.begin(), text.end(), ws_re, -1),
            std::sregex_token_iterator(),
            std::ostream_iterator<std::string>(std::cout, "\n"));
  std::cout << '\n';
  // Iterating the first submatches
  const std::string html = R"(<p><a href="http://google.com">google</a> )"
                           R"(< a HREF ="http://cppreference.com">cppreference</a>\n</p>)";
  const std::regex url_re(R"!!(<\s*A\s+[^>]*href\s*=\s*"([^"]*)")!!",
                          std::regex::icase);
  std::copy(std::sregex_token_iterator(html.begin(), html.end(), url_re, 1),
            std::sregex_token_iterator(),
            std::ostream_iterator<std::string>(std::cout, "\n"));
}
} // namespace regex_token_iterator_test
