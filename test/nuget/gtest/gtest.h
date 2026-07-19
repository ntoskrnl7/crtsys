#pragma once

#include <cstring>
#include <iostream>

namespace testing {
using test_function = void (*)();

struct test_case {
  const char* suite;
  const char* name;
  test_function function;
};

inline test_case** registry() {
  static test_case* entries[256] = {};
  return entries;
}

inline unsigned& registry_size() {
  static unsigned value = 0;
  return value;
}

inline int& failure_count() {
  static int value = 0;
  return value;
}

class test_registrar {
public:
  test_registrar(const char* suite, const char* name, test_function function)
      : test_{suite, name, function} {
    auto& size = registry_size();
    if (size < 256) {
      registry()[size++] = &test_;
    } else {
      ++failure_count();
      std::cerr << "Too many registered tests\n";
    }
  }

private:
  test_case test_;
};

inline void InitGoogleTest() {}

inline void InitGoogleTest(int*, char**) {}

inline void record_failure(const char* file, int line, const char* expression) {
  ++failure_count();
  std::cerr << file << ':' << line << ": expectation failed: " << expression
            << '\n';
}

inline int RunAllTests() {
  const auto size = registry_size();
  for (unsigned i = 0; i < size; ++i) {
    auto* test = registry()[i];
    std::cout << "[ RUN      ] " << test->suite << '.' << test->name << '\n';
    test->function();
  }

  if (failure_count() != 0) {
    std::cerr << failure_count() << " expectation(s) failed\n";
    return 1;
  }

  std::cout << "[  PASSED  ] " << size << " test(s)\n";
  return 0;
}
} // namespace testing

#define GTEST_JOIN_IMPL(a, b) a##b
#define GTEST_JOIN(a, b) GTEST_JOIN_IMPL(a, b)

#define TEST(suite_name, test_name)                                           \
  static void GTEST_JOIN(suite_name, GTEST_JOIN(_, test_name))();             \
  static ::testing::test_registrar GTEST_JOIN(                                \
      GTEST_JOIN(suite_name, GTEST_JOIN(_, test_name)), _registrar)(          \
      #suite_name, #test_name,                                                \
      &GTEST_JOIN(suite_name, GTEST_JOIN(_, test_name)));                     \
  static void GTEST_JOIN(suite_name, GTEST_JOIN(_, test_name))()

#define EXPECT_TRUE(condition)                                                \
  do {                                                                        \
    if (!(condition)) {                                                       \
      ::testing::record_failure(__FILE__, __LINE__,                           \
                                "EXPECT_TRUE(" #condition ")");             \
    }                                                                         \
  } while (false)

#define EXPECT_FALSE(condition)                                               \
  do {                                                                        \
    if (condition) {                                                          \
      ::testing::record_failure(__FILE__, __LINE__,                           \
                                "EXPECT_FALSE(" #condition ")");            \
    }                                                                         \
  } while (false)

#define EXPECT_EQ(actual, expected)                                           \
  do {                                                                        \
    const auto& actual_value = (actual);                                      \
    const auto& expected_value = (expected);                                  \
    if (!(actual_value == expected_value)) {                                  \
      ::testing::record_failure(__FILE__, __LINE__,                           \
                                "EXPECT_EQ(" #actual ", " #expected ")");   \
    }                                                                         \
  } while (false)

#define ASSERT_EQ(actual, expected)                                           \
  do {                                                                        \
    const auto& actual_value = (actual);                                      \
    const auto& expected_value = (expected);                                  \
    if (!(actual_value == expected_value)) {                                  \
      ::testing::record_failure(__FILE__, __LINE__,                           \
                                "ASSERT_EQ(" #actual ", " #expected ")");   \
      return;                                                                 \
    }                                                                         \
  } while (false)

#define EXPECT_NE(actual, expected)                                           \
  do {                                                                        \
    const auto& actual_value = (actual);                                      \
    const auto& expected_value = (expected);                                  \
    if (!(actual_value != expected_value)) {                                  \
      ::testing::record_failure(__FILE__, __LINE__,                           \
                                "EXPECT_NE(" #actual ", " #expected ")");   \
    }                                                                         \
  } while (false)

#define EXPECT_GT(actual, expected)                                           \
  do {                                                                        \
    const auto& actual_value = (actual);                                      \
    const auto& expected_value = (expected);                                  \
    if (!(actual_value > expected_value)) {                                   \
      ::testing::record_failure(__FILE__, __LINE__,                           \
                                "EXPECT_GT(" #actual ", " #expected ")");   \
    }                                                                         \
  } while (false)

#define EXPECT_STREQ(actual, expected)                                        \
  do {                                                                        \
    const auto* actual_value = (actual);                                      \
    const auto* expected_value = (expected);                                  \
    if (std::strcmp(actual_value, expected_value) != 0) {                     \
      ::testing::record_failure(__FILE__, __LINE__,                           \
                                "EXPECT_STREQ(" #actual ", " #expected      \
                                ")");                                        \
    }                                                                         \
  } while (false)

#define EXPECT_NO_FATAL_FAILURE(statement)                                    \
  do {                                                                        \
    statement;                                                                \
  } while (false)

#define EXPECT_THROW(statement, exception_type)                               \
  do {                                                                        \
    bool caught_expected_exception = false;                                   \
    try {                                                                     \
      statement;                                                              \
    } catch (const exception_type&) {                                          \
      caught_expected_exception = true;                                       \
    } catch (...) {                                                           \
    }                                                                         \
    if (!caught_expected_exception) {                                         \
      ::testing::record_failure(                                              \
          __FILE__, __LINE__,                                                  \
          "EXPECT_THROW(" #statement ", " #exception_type ")");          \
    }                                                                         \
  } while (false)

#define RUN_ALL_TESTS() ::testing::RunAllTests()
