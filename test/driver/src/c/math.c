#include <stdint.h>
#include <stdio.h>

typedef char bool;
#define true 1
#define false 0

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
  uint8_t i8 = (uint8_t)f;
  uint16_t i16 = (uint16_t)f;
  uint32_t i32 = (uint32_t)f;
  uint64_t i64 = (uint64_t)f;
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
  uint8_t i8 = (uint8_t)d;
  uint16_t i16 = (uint16_t)d;
  uint32_t i32 = (uint32_t)d;
  uint64_t i64 = (uint64_t)d;
  return i8 + i16 + i32 + i64 == 4;
}

bool integer_to_double() {
  double d_i8 = (uint8_t)1;
  double d_i16 = (uint8_t)1;
  double d_i32 = (uint8_t)1;
  double d_i64 = (uint8_t)1;
  return d_i8 + d_i16 + d_i32 + d_i64 == 4;
}

#include <math.h>

void pow_test() {
  int actual;
  int excepted = 1;
  for (int i = 0; i < 20; i++, excepted <<= 1) {
    actual = (int)pow(2, i);
    if (excepted != actual)
      fprintf(stderr, "pow(2, %d) faield - excepted = %d, actual =%d\n", i,
              excepted, actual);
  }
}

void math_test() {
  if (!float_to_integer())
    fprintf(stderr, "float_to_integer failed\n");

  if (!integer_to_float())
    fprintf(stderr, "integer_to_float failed\n");

  if (!double_to_integer())
    fprintf(stderr, "double_to_integer failed\n");

  if (!integer_to_double())
    fprintf(stderr, "integer_to_double failed\n");

  pow_test();
}