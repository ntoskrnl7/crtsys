#pragma warning(disable : 4100 4210)

#include <math.h>
#include <stdint.h>

// error C2169: 'pow' : intrinsic function, cannot be defined
#pragma function(pow)

// error LNK2019: unresolved external symbol __libm_sse2_pow_precise referenced
// in function double __stdcall pow<int,int,0>(int,int)
EXTERN_C double __cdecl _libm_sse2_pow_precise(double x, double y) {
  return pow(x, y);
}

//
// https://github.com/SpoilerScriptsGroup/RetrievAL/blob/develop/SpoilerAL-winmm.dll/crt/math/pow.c
//
#ifndef _M_IX86
#include <errno.h>
#include <float.h>
#include <math.h>

#ifndef EXTERN_C
#ifdef __cplusplus
#define EXTERN_C extern "C"
#else
#define EXTERN_C extern
#endif
#endif

#define USE_LONGDOUBLE 0

#if USE_LONGDOUBLE
#include "longdouble.h"
#endif

EXTERN_C double __cdecl pow(double x, double y) {
  int is_int = ((x == (uint64_t)x) && (y == (uint64_t)y));
  if (is_int) {
    if (x == 1 || y == 0)
      return 1;
    uint64_t ix = (uint64_t)x;
    uint64_t iy = (uint64_t)y;
    if (ix % 2 == 0) {
      return (double)(2 << ((iy * (ix / 2)) - 1));
    }
  }
  if (y) {
    if (x) {
      double dummy;

      if (x >= 0 || !modf(y, &dummy)) {
        unsigned char sign;
#if USE_LONGDOUBLE
        uint16_t cw;
        longdouble s, e;
#endif

        sign = 0;
        if (x < 0) {
          x = -x;
          if (modf(y * 0.5, &dummy)) {
            sign = 1;
          }
        }
#if !USE_LONGDOUBLE
        x = log(x);
        if (x < 0) {
          x = -x;
          y = -y;
        }
        if (x <= 1 || y <= DBL_MAX / x) {
          x = exp(x * y);
        } else {
          errno = ERANGE;
          x = HUGE_VAL;
        }
#else
#ifdef __cplusplus
        cw = longdouble::fstcw();
        longdouble::fldcw((cw & ~X87_MCW_RC) | X87_PC_64 | X87_EM_UNDERFLOW |
                          X87_EM_OVERFLOW);
        s = x;
        s = s.fxtract(&e);
        s = s.fyl2x(1);
        s = s + e;
        s = s * y;
        e = s.frndint();
        s = s - e;
        s = s.f2xm1();
        s = s + 1;
        s = s.fscale(e);
        x = (double)s;
        longdouble::fclex();
        longdouble::fldcw(cw);
#else
        cw = _fstcw();
        _fldcw((cw & ~X87_MCW_RC) | X87_PC_64 | X87_EM_UNDERFLOW |
               X87_EM_OVERFLOW);
        s = _fld_r8(x);
        s = _fxtract(s, &e);
        s = _fyl2x(s, _fld1());
        s = _fadd(s, e);
        s = _fmul(s, _fld_r8(y));
        e = s.frndint();
        s = _fsub(s, e);
        s = _f2xm1(s);
        s = _fadd(s, _fld1());
        s = _fscale(s, e);
        x = _fst_r8(s);
        _fclex();
        _fldcw(cw);
#endif
        if (fabs(x) > DBL_MAX) {
          errno = ERANGE;
        }
#endif
        if (sign) {
          x = -x;
        }
      } else {
        x = 0;
        errno = EDOM;
      }
    } else if (y < 0) {
      errno = EDOM;
    }
  } else {
    x = 1;
  }
  return x;
}
#else
#include <errno.h>

#ifndef EXTERN_C
#ifdef __cplusplus
#define EXTERN_C extern "C"
#else
#define EXTERN_C extern
#endif
#endif

#define X87_MCW_EM 0x003F
#define X87_EM_INVALID 0x0001
#define X87_EM_DENORMAL 0x0002
#define X87_EM_ZERODIVIDE 0x0004
#define X87_EM_OVERFLOW 0x0008
#define X87_EM_UNDERFLOW 0x0010
#define X87_EM_INEXACT 0x0020
#define X87_MCW_PC 0x0300
#define X87_PC_24 0x0100
#define X87_PC_53 0x0200
#define X87_PC_64 0x0300
#define X87_MCW_RC 0x0C00
#define X87_RC_NEAR 0x0000
#define X87_RC_DOWN 0x0400
#define X87_RC_UP 0x0800
#define X87_RC_CHOP 0x0C00
#define X87_MCW_IC 0x1000
#define X87_IC_PROJECTIVE 0x0000
#define X87_IC_AFFINE 0x1000
#define X87_MCW_DN 0x8040
#define X87_DN_SAVE 0x0000
#define X87_DN_FLUSH_OPERANDS_SAVE_RESULTS 0x0040
#define X87_DN_SAVE_OPERANDS_FLUSH_RESULTS 0x8000
#define X87_DN_FLUSH 0x8040
#define X87_CW_DEFAULT 0x027F

EXTERN_C const double fpconst_half;
EXTERN_C const double fpconst_one;
#define _half fpconst_half
#define _one fpconst_one

EXTERN_C __declspec(naked) double __cdecl pow(double x, double y) {
  double __cdecl _CIpow(/*st1 x, st0 y*/);

  __asm
  {
		fld     qword ptr [esp + 4]             ; Load real from stack
		fld     qword ptr [esp + 12]            ; Load real from stack
		jmp     _CIpow
  }
}

EXTERN_C __declspec(naked) double __cdecl _CIpow(/*st1 x, st0 y*/) {
#ifndef DISABLE_UCRT
  errno_t *__cdecl _errno();
#define set_errno(x, n)                                                        \
  __asm sub esp,                                                               \
      n                         /* Allocate temporary space */                 \
      __asm fstp qword ptr[esp] /* Save x */                                   \
      __asm call _errno         /* Get C errno variable pointer */             \
      __asm fld qword ptr[esp]  /* Load x */                                   \
      __asm add esp,                                                           \
      n /* Deallocate temporary space */                                       \
      __asm mov dword ptr[eax],                                                \
      x /* Set error number */
#else
  extern errno_t _terrno;
#define set_errno(x, n) __asm mov dword ptr[_terrno], x /* Set error number */
#endif

  __asm {
		ftst                                    ; Compare y with zero
		fstsw   ax                              ; Get the FPU status word
		test    ah, 40H                         ; y != 0 ?
		jz      L1                              ; Re-direct if y != 0
		fld1                                    ; Load real number 1
		jmp     L8                              ; End of case

		align   16
	L1:
		mov     dx, ax                          ; Save flags of compare y with zero
		fstcw   word ptr [esp - 4]              ; Save control word
		fxch                                    ; Swap st, st(1)
		ftst                                    ; Compare x with zero
		fstsw   ax                              ; Get the FPU status word
		sahf                                    ; Store AH into Flags
		mov     ax, dx                          ; Load flags of compare y with zero
		mov     dl, 40H                         ; Set not negation flag
		mov     cx, word ptr [esp - 4]          ;
		jb      L2                              ; Re-direct if x < 0
		ja      L5                              ; Re-direct if x > 0
		test    ah, 01H                         ; y < 0 ?
		jnz     L3                              ; Re-direct if y < 0
		jmp     L8                              ; End of case

		align   16
	L2:
		fld     st(1)                           ; Duplicate y as st1
		frndint                                 ; Round to integer
		fcomip  st(0), st(2)                    ; y = int(y) ?
		je      L4                              ; Proceed if y = int(y)
		fldz                                    ; Set result to zero
	L3:
		set_errno(EDOM, 8)                      ; Set domain error (EDOM)
		jmp     L8                              ; End of case

		align   16
	L4:
		or      cx, X87_RC_CHOP                  ; Modify control word
		mov     word ptr [esp - 8], cx          ;
		fldcw   word ptr [esp - 8]              ; Set new control word
		fchs                                    ; Set x = -x
		fld     st(1)                           ; Duplicate y as st1
		fmul    qword ptr [_half]               ; Compute y * 0.5
		fld     st(0)                           ; Duplicate result
		frndint                                 ; Round to integer
		fsub                                    ; Subtract
		ftst                                    ; Compare result with zero
		fstsw   ax                              ; Get the FPU status word
		fstp    st(0)                           ; Set new top of stack
		and     dl, ah                          ; Set bit if y is even
	L5:
		and     cx, not X87_MCW_RC              ; Modify control word
		or      cx, X87_PC_64        or \
		            X87_EM_UNDERFLOW or \
		            X87_EM_OVERFLOW              ;
		mov     word ptr [esp - 8], cx          ;
		fldcw   word ptr [esp - 8]              ; Set new control word
		fld     st(0)                           ; Duplicate x
		fxtract                                 ; Get exponent and significand  s = significand, e = exponent
		fld1                                    ; Load real number 1
		fxch                                    ; Swap st, st(1)
		fyl2x                                   ; Compute the natural log(x)    s = fyl2x(s, 1)
		fadd                                    ; Add                           s += e
		fmul    st(0), st(2)                    ; Multiply                      s *= y
		fld     st(0)                           ; Duplicate s
		frndint                                 ; Round to integer              e = frndint(s)
		fxch                                    ; Swap st, st(1)
		fsub    st(0), st(1)                    ; Subtract                      s -= e
		f2xm1                                   ; Compute 2 to the (x - 1)      s = f2xm1(s)
		fadd    qword ptr [_one]                ; Add                           s += 1
		fscale                                  ; Scale by power of 2           x = fscale(s, e)
		fstp    st(1)                           ; Set new stack top and pop
		fst     qword ptr [esp - 12]            ; Save x, cast to qword
		fld     qword ptr [esp - 12]            ; Load x
		fclex                                   ; Clear exceptions
		fldcw   word ptr [esp - 4]              ; Restore control word
		fxam                                    ; Examine st
		fstsw   ax                              ; Get the FPU status word
		and     ah, 45H                         ; Isolate C0, C2 and C3
		cmp     ah, 05H                         ; Infinity ?
		je      L6                              ; Re-direct if x is infinity
		fstp    st(0)                           ; Set new top of stack
		jmp     L7                              ; End of case

		align   16
	L6:
		fstp    st(1)                           ; Set new stack top and pop
#ifndef DISABLE_UCRT
		mov     byte ptr [esp - 4], dl          ; Save negation flag
		set_errno(ERANGE, 12)                   ; Set range error (ERANGE)
		mov     dl, byte ptr [esp - 4]          ; Load negation flag
#else
		set_errno(ERANGE, 8)                    ; Set range error (ERANGE)
#endif
	L7:
		test    dl, dl                          ; Negation required ?
		jnz     L8                              ; No, re-direct
		fchs                                    ; Negate the result
	L8:
		fstp    st(1)                           ; Set new stack top and pop
		fstp    st(1)                           ; Set new stack top and pop
		ret
  }

#undef set_errno
}
#endif
#pragma warning(default : 4100 4210)