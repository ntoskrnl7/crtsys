//
// https://github.com/bminor/musl/blob/master/src/internal/libm.h
//
#include <stdint.h>
#include <math.h>
#include <limits.h>

/* fp_force_eval ensures that the input value is computed when that's
   otherwise unused.  To prevent the constant folding of the input
   expression, an additional fp_barrier may be needed or a compilation
   mode that does so (e.g. -frounding-math in gcc). Then it can be
   used to evaluate an expression for its fenv side-effects only.   */

#ifndef fp_force_evalf
#define fp_force_evalf fp_force_evalf
static inline void fp_force_evalf(float x)
{
	volatile float y;
	y = x;
}
#endif

#ifndef fp_force_eval
#define fp_force_eval fp_force_eval
static inline void fp_force_eval(double x)
{
	volatile double y;
	y = x;
}
#endif

#ifndef fp_force_evall
#define fp_force_evall fp_force_evall
static inline void fp_force_evall(long double x)
{
	volatile long double y;
	y = x;
}
#endif

#define FORCE_EVAL(x) do {                        \
	if (sizeof(x) == sizeof(float)) {         \
		fp_force_evalf((float)x);                \
	} else if (sizeof(x) == sizeof(double)) { \
		fp_force_eval((double)x);                 \
	} else {                                  \
		fp_force_evall(x);                \
	}                                         \
} while(0)

#pragma warning(push)
#pragma warning(disable:4127)
#pragma warning(disable:4244)

//
// googletest/src/gtest.cc (testing::internal::DoubleNearPredFormat -> nextafter)
//
_Check_return_ _ACRTIMP double __cdecl nextafter(_In_ double x, _In_ double y)
{
    //
    // https://github.com/bminor/musl/blob/master/src/math/nextafter.c
    //
	union {double f; uint64_t i;} ux={x}, uy={y};
	uint64_t ax, ay;
	int e;

	if (isnan(x) || isnan(y))
		return x + y;
	if (ux.i == uy.i)
		return y;
	ax = ux.i & ULLONG_MAX/2;
	ay = uy.i & ULLONG_MAX/2;
	if (ax == 0) {
		if (ay == 0)
			return y;
		ux.i = (uy.i & 1ULL<<63) | 1;
	} else if (ax > ay || ((ux.i ^ uy.i) & 1ULL<<63))
		ux.i--;
	else
		ux.i++;
	e = ux.i >> 52 & 0x7ff;
	/* raise overflow if ux.f is infinite and x is finite */
	if (e == 0x7ff)
		FORCE_EVAL(x+x);
	/* raise underflow if ux.f is subnormal or zero */
	if (e == 0)
		FORCE_EVAL(x*x + ux.f*ux.f);
	return ux.f;
}
#pragma warning(pop)