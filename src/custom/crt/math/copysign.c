#include <math.h>
#include <stdint.h>

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
