#include <math.h>
#include <stdint.h>

#if _MSC_VER >= 1930 && (defined(_M_X64) || defined(_M_ARM64))
#pragma function(log2, log2f)
#endif

static double __cdecl crtsys_positive_infinity(void) {
  union {
    uint64_t bits;
    double value;
  } value_bits = {0x7ff0000000000000ULL};

  return value_bits.value;
}

static double __cdecl crtsys_quiet_nan(void) {
  union {
    uint64_t bits;
    double value;
  } value_bits = {0x7ff8000000000000ULL};

  return value_bits.value;
}

static double __cdecl crtsys_log2_mantissa(_In_ double mantissa) {
  const double inverse_ln2 = 1.44269504088896340735992468100189214;
  const double z = (mantissa - 1.0) / (mantissa + 1.0);
  const double z_squared = z * z;
  double term = z;
  double sum = 0.0;

  for (int denominator = 1; denominator <= 63; denominator += 2) {
    sum += term / (double)denominator;
    term *= z_squared;
  }

  return 2.0 * sum * inverse_ln2;
}

_Check_return_ _ACRTIMP double __cdecl log2(_In_ double x) {
  union {
    double value;
    uint64_t bits;
  } value_bits = {x};

  const uint64_t sign = value_bits.bits & 0x8000000000000000ULL;
  uint64_t magnitude = value_bits.bits & 0x7fffffffffffffffULL;
  uint64_t exponent = (magnitude >> 52) & 0x7ffULL;
  const uint64_t fraction = magnitude & 0x000fffffffffffffULL;

  if (exponent == 0x7ffULL) {
    return fraction == 0 ? x : x + x;
  }

  if (magnitude == 0) {
    return -crtsys_positive_infinity();
  }

  if (sign != 0) {
    return crtsys_quiet_nan();
  }

  int exponent_value = 0;
  if (exponent == 0) {
    value_bits.value *= 18014398509481984.0; // 2^54
    magnitude = value_bits.bits & 0x7fffffffffffffffULL;
    exponent = (magnitude >> 52) & 0x7ffULL;
    exponent_value = (int)exponent - 1023 - 54;
  } else {
    exponent_value = (int)exponent - 1023;
  }

  value_bits.bits = (magnitude & 0x000fffffffffffffULL) |
                    0x3ff0000000000000ULL;
  return (double)exponent_value + crtsys_log2_mantissa(value_bits.value);
}

_Check_return_ _ACRTIMP float __cdecl log2f(_In_ float x) {
  return (float)log2((double)x);
}

_Check_return_ _ACRTIMP long double __cdecl log2l(_In_ long double x) {
  return (long double)log2((double)x);
}
