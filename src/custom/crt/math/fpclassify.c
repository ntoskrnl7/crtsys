#include <math.h>
#include <stdint.h>

#pragma warning(push)
#pragma warning(disable : 4204)

#if _MSC_VER >= 1920
_Check_return_ _ACRTIMP short __cdecl _dtest(_In_ double *_Px);
#pragma function(_dtest)
#endif
_Check_return_ _ACRTIMP short __cdecl _dtest(_In_ double *_Px) {
  //
  // https://github.com/bminor/musl/blob/master/src/math/__fpclassify.c
  //
  union {
    double f;
    uint64_t i;
  } u = {*_Px};
  int e = u.i >> 52 & 0x7ff;
  if (!e)
    return u.i << 1 ? FP_SUBNORMAL : FP_ZERO;
  if (e == 0x7ff)
    return u.i << 12 ? FP_NAN : FP_INFINITE;
  return FP_NORMAL;
}

#if _MSC_VER >= 1920
_Check_return_ _ACRTIMP short __cdecl _dclass(_In_ double _X);
#pragma function(_dclass)
#endif
_Check_return_ _ACRTIMP short __cdecl _dclass(_In_ double _X) {
  return _dtest(&_X);
}
#pragma warning(pop)