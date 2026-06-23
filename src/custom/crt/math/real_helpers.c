#include <limits.h>
#include <math.h>
#include <stdint.h>

#if defined(_M_IX86)
#define CRTSYS_MATH_HELPER_CALL __stdcall
#else
#define CRTSYS_MATH_HELPER_CALL __cdecl
#endif

short CRTSYS_MATH_HELPER_CALL _Dscale(double* px, long lexp);

_Check_return_ _ACRTIMP double __cdecl copysign(_In_ double number,
                                                _In_ double sign) {
  union {
    double value;
    uint64_t bits;
  } number_bits = {number}, sign_bits = {sign};

  number_bits.bits = (number_bits.bits & 0x7fffffffffffffffULL) |
                     (sign_bits.bits & 0x8000000000000000ULL);
  return number_bits.value;
}

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

_Check_return_ _ACRTIMP double __cdecl scalbn(_In_ double x, _In_ int n) {
  _Dscale(&x, (long)n);
  return x;
}
