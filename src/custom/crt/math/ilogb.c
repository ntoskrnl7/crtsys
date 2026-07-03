#include <limits.h>
#include <math.h>
#include <stdint.h>

_Check_return_ _ACRTIMP int __cdecl ilogb(_In_ double x) {
  union {
    double value;
    uint64_t bits;
  } value_bits = {x};

  const uint64_t magnitude = value_bits.bits & 0x7fffffffffffffffULL;
  const uint64_t exponent = (magnitude >> 52) & 0x7ffULL;
  const uint64_t fraction = magnitude & 0x000fffffffffffffULL;

  if (magnitude == 0) {
    return FP_ILOGB0;
  }

  if (exponent == 0x7ffULL) {
    return fraction == 0 ? INT_MAX : FP_ILOGBNAN;
  }

  if (exponent != 0) {
    return (int)exponent - 1023;
  }

  uint64_t normalized = fraction;
  int result = -1022;
  while ((normalized & 0x0010000000000000ULL) == 0) {
    normalized <<= 1;
    --result;
  }

  return result;
}
