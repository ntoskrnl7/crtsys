#include <limits.h>
#include <math.h>
#include <stdint.h>

#if _MSC_VER >= 1930 &&                                                    \
    (defined(_M_X64) || defined(_M_ARM64) || defined(_M_ARM64EC))
#pragma function(log2, log2f)
#endif

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

_Check_return_ _ACRTIMP float __cdecl copysignf(_In_ float number,
                                                _In_ float sign) {
  union {
    float value;
    uint32_t bits;
  } number_bits = {number}, sign_bits = {sign};

  number_bits.bits =
      (number_bits.bits & 0x7fffffffUL) | (sign_bits.bits & 0x80000000UL);
  return number_bits.value;
}

_Check_return_ _ACRTIMP long double __cdecl copysignl(_In_ long double number,
                                                      _In_ long double sign) {
  return (long double)copysign((double)number, (double)sign);
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
  return ldexp(x, n);
}

// MSVC STL <cmath> integer-overload tests can instantiate std::log2 through
// the CRT entry point on x86 driver builds.
_Check_return_ _ACRTIMP double __cdecl log2(_In_ double x) {
  return log(x) * 1.44269504088896340735992468100189214;
}

_Check_return_ _ACRTIMP float __cdecl log2f(_In_ float x) {
  return (float)log2((double)x);
}

_Check_return_ _ACRTIMP long double __cdecl log2l(_In_ long double x) {
  return (long double)log2((double)x);
}
