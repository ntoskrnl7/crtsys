//
// https://en.cppreference.com/w/cpp/regex#Example
//
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
