#include <math.h>

#if defined(_M_IX86) || defined(_M_X64)
#include <immintrin.h>
#include <intrin.h>

#pragma intrinsic(__cpuid)
#pragma intrinsic(_xgetbv)
#elif defined(_M_ARM64)
#include <arm64_neon.h>
#endif

#pragma function(fma, fmaf, fmal)

#if defined(_M_IX86) || defined(_M_X64)
#define CRTSYS_CPUID_ECX_FMA 0x00001000
#define CRTSYS_CPUID_ECX_OSXSAVE 0x08000000
#define CRTSYS_CPUID_ECX_AVX 0x10000000
#define CRTSYS_XCR0_SSE 0x00000002
#define CRTSYS_XCR0_YMM 0x00000004

static int __cdecl crtsys_fma3_available(void) {
  int cpu_info[4];

  __cpuid(cpu_info, 0);
  if (cpu_info[0] < 1) {
    return 0;
  }

  __cpuid(cpu_info, 1);
  if ((cpu_info[2] & (CRTSYS_CPUID_ECX_FMA | CRTSYS_CPUID_ECX_OSXSAVE |
                      CRTSYS_CPUID_ECX_AVX)) !=
      (CRTSYS_CPUID_ECX_FMA | CRTSYS_CPUID_ECX_OSXSAVE |
       CRTSYS_CPUID_ECX_AVX)) {
    return 0;
  }

  return (_xgetbv(0) & (CRTSYS_XCR0_SSE | CRTSYS_XCR0_YMM)) ==
         (CRTSYS_XCR0_SSE | CRTSYS_XCR0_YMM);
}
#endif

// Kernel entry points for MSVC STL/UCRT math dependencies. Use hardware fused
// multiply-add where MSVC exposes it, with a non-fused fallback so the driver
// stays self-contained.
_Check_return_ _ACRTIMP double __cdecl fma(_In_ double x, _In_ double y,
                                           _In_ double z) {
#if defined(_M_IX86) || defined(_M_X64)
  if (crtsys_fma3_available()) {
    return __fmadd_sd(x, y, z);
  }
  return x * y + z;
#elif defined(_M_ARM64)
  return vget_lane_f64(vfma_f64(vdup_n_f64(z), vdup_n_f64(x), vdup_n_f64(y)),
                       0);
#else
  return x * y + z;
#endif
}

_Check_return_ _ACRTIMP float __cdecl fmaf(_In_ float x, _In_ float y,
                                           _In_ float z) {
#if defined(_M_IX86) || defined(_M_X64)
  if (crtsys_fma3_available()) {
    return __fmadd_ss(x, y, z);
  }
  return (float)((double)x * (double)y + (double)z);
#elif defined(_M_ARM64)
  return vget_lane_f32(vfma_f32(vdup_n_f32(z), vdup_n_f32(x), vdup_n_f32(y)),
                       0);
#else
  return (float)((double)x * (double)y + (double)z);
#endif
}

_Check_return_ _ACRTIMP long double __cdecl fmal(_In_ long double x,
                                                 _In_ long double y,
                                                 _In_ long double z) {
#if defined(_M_IX86) || defined(_M_X64)
  if (crtsys_fma3_available()) {
    return (long double)__fmadd_sd((double)x, (double)y, (double)z);
  }
  return x * y + z;
#elif defined(_M_ARM64)
  return (long double)vget_lane_f64(
      vfma_f64(vdup_n_f64((double)z), vdup_n_f64((double)x),
               vdup_n_f64((double)y)),
      0);
#else
  return x * y + z;
#endif
}
