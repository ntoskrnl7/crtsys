#include <cassert>
#include <iostream>
#include <typeinfo>

namespace typeid_test {
struct base {
  virtual ~base() = default;
};

struct derived : base {};

void run() {
  derived object;
  base &as_base = object;

  const std::type_info &dynamic_type = typeid(as_base);
  const std::type_info &expected_type = typeid(derived);

  assert(dynamic_type == expected_type);
  // Release builds compile out assert(), so mark assert-only locals as used.
  (void)dynamic_type;
  (void)expected_type;
  std::cout << "typeid resolved the dynamic type\n";
}
} // namespace typeid_test

namespace dynamic_cast_test {
struct root {
  virtual ~root() = default;
};

struct left : virtual root {
  int left_value = 1;
};

struct right : virtual root {
  int right_value = 2;
};

struct child : left, right {
  int child_value = 3;
};

struct unrelated : root {};

void run() {
  child object;
  left *as_left = &object;

  right *as_right = dynamic_cast<right *>(as_left);
  root *as_root = dynamic_cast<root *>(as_left);
  unrelated *as_unrelated = dynamic_cast<unrelated *>(as_left);

  assert(as_right != nullptr);
  assert(as_right->right_value == 2);
  assert(as_root != nullptr);
  assert(as_unrelated == nullptr);
  // Release builds compile out assert(), so mark assert-only locals as used.
  (void)as_right;
  (void)as_root;
  (void)as_unrelated;

  std::cout << "dynamic_cast resolved cross-cast and failed cast paths\n";
}
} // namespace dynamic_cast_test
