//
// Google Test.
//
#include <gtest/gtest.h>

#include <nlohmann/json.hpp>
TEST(nlohmann_json_test, basic_test) {

  // create an empty structure (null)
  nlohmann::json j;

  // add a number that is stored as double (note the implicit conversion of j to
  // an object)
  j["pi"] = 3.141;

  // add a Boolean that is stored as bool
  j["happy"] = true;

  // add a string that is stored as std::string
  j["name"] = "Niels";

  // add another null object by passing nullptr
  j["nothing"] = nullptr;

  // add an object inside the object
  j["answer"]["everything"] = 42;

  // add an array that is stored as std::vector (using an initializer list)
  j["list"] = {1, 0, 2};

  // add another object (using an initializer list of pairs)
  j["object"] = {{"currency", "USD"}, {"value", 42.99}};

  // instead, you could also write (which looks very similar to the JSON above)
  nlohmann::json j2 = {{"pi", 3.141},
                       {"happy", true},
                       {"name", "Niels"},
                       {"nothing", nullptr},
                       {"answer", {{"everything", 42}}},
                       {"list", {1, 0, 2}},
                       {"object", {{"currency", "USD"}, {"value", 42.99}}}};

  EXPECT_STREQ(j.dump().c_str(), j2.dump().c_str());
}

TEST(nlohmann_json_test, literal_test) {
  // create object from string literal
  nlohmann::json j = "{ \"happy\": true, \"pi\": 3.141 }"_json;

  // or even nicer with a raw string literal
  auto j2 = R"(
  {
    "happy": true,
    "pi": 3.141
  }
)"_json;

  EXPECT_STREQ(j.dump().c_str(), j2.dump().c_str());
}