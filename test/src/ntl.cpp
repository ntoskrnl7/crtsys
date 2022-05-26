#include <ntl/expand_stack>

#include <string>

bool ntl_expand_stack_test() {
  long result = 0;
  ntl::expand_stack(
      [&result](int i, long l, double d) { result = (long)(i + l + d); }, 1, 2,
      3.0);
  if (result != 6)
    return false;

  result = ntl::expand_stack(
      [](int i, long l, double d) -> long { return (long)(i + l + d); }, 1, 2,
      3.0);
  if (result != 6)
    return false;

  std::string t1;
  std::string t2;
  std::string t3;

  ntl::expand_stack([&] {
    try {
      throw std::bad_alloc();
    } catch (const std::exception &e) {
      try {
        t1 = e.what();
        throw std::bad_array_new_length();
      } catch (const std::exception &e) {
        try {
          t2 = e.what();
          throw std::runtime_error("test");
        } catch (const std::runtime_error &e) {
          t3 = e.what();
        } catch (const std::exception &) {
        }
      }
    }
  });
  return !(t1.empty() || t2.empty() || t3.empty());
}

//
// Google Test.
//
#include <gtest/gtest.h>

TEST(ntl_test, ntl_expand_stack_test) { EXPECT_TRUE(ntl_expand_stack_test()); }
