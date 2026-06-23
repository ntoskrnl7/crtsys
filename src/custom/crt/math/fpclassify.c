#include <math.h>
#include <stdint.h>

#pragma warning(push)
#pragma warning(disable : 4204)

#if _MSC_VER >= 1920
_Check_return_ _ACRTIMP short __cdecl _fdtest(_In_ float *_Px);
#pragma function(_fdtest)
#endif
_Check_return_ _ACRTIMP short __cdecl _fdtest(_In_ float *_Px) {
  //
  // https://github.com/bminor/musl/blob/master/src/math/__fpclassifyf.c
  //
  union {
    float f;
    uint32_t i;
  } u = {*_Px};
  int e = u.i >> 23 & 0xff;
  if (!e)
    return u.i << 1 ? FP_SUBNORMAL : FP_ZERO;
  if (e == 0xff)
    return u.i << 9 ? FP_NAN : FP_INFINITE;
  return FP_NORMAL;
}

#if _MSC_VER >= 1920
_Check_return_ _ACRTIMP short __cdecl _fdclass(_In_ float _X);
#pragma function(_fdclass)
#endif
_Check_return_ _ACRTIMP short __cdecl _fdclass(_In_ float _X) {
  return _fdtest(&_X);
}

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

#if _MSC_VER >= 1920
_Check_return_ _ACRTIMP short __cdecl _ldtest(_In_ long double *_Px);
#pragma function(_ldtest)
#endif
_Check_return_ _ACRTIMP short __cdecl _ldtest(_In_ long double *_Px) {
  return _dtest((double *)_Px);
}

#if _MSC_VER >= 1920
_Check_return_ _ACRTIMP short __cdecl _ldclass(_In_ long double _X);
#pragma function(_ldclass)
#endif
_Check_return_ _ACRTIMP short __cdecl _ldclass(_In_ long double _X) {
  return _ldtest(&_X);
}
#pragma warning(pop)
