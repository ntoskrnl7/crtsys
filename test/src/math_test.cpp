#include <stdint.h>

bool int64_t_test() {
  volatile int64_t s = 1;
  s += s;
  s -= s;
  s *= s;
  s /= s;
  s %= s;
  s >>= 33;
  s <<= 33;
  return true;
}

bool uint64_t_test() {
  volatile uint64_t u = 1;
  u += u;
  u -= u;
  u *= u;
  u /= u;
  u %= u;
  u >>= 33;
  u <<= 33;
  return true;
}

bool float_to_integer() {
  float f = 1.6f;
  auto i8 = (uint8_t)f;
  auto i16 = (uint16_t)f;
  auto i32 = (uint32_t)f;
  auto i64 = (uint64_t)f;
  return i8 + i16 + i32 + i64 == 4;
}

bool integer_to_float() {
  float f_i8 = (uint8_t)1;
  float f_i16 = (uint8_t)1;
  float f_i32 = (uint8_t)1;
  float f_i64 = (uint8_t)1;
  return f_i8 + f_i16 + f_i32 + f_i64 == 4;
}

bool double_to_integer() {
  double d = 1.6f;
  auto i8 = (uint8_t)d;
  auto i16 = (uint16_t)d;
  auto i32 = (uint32_t)d;
  auto i64 = (uint64_t)d;
  return i8 + i16 + i32 + i64 == 4;
}

bool integer_to_double() {
  double d_i8 = (uint8_t)1;
  double d_i16 = (uint8_t)1;
  double d_i32 = (uint8_t)1;
  double d_i64 = (uint8_t)1;
  return d_i8 + d_i16 + d_i32 + d_i64 == 4;
}