//
// https://github.com/SpoilerScriptsGroup/RetrievAL/blob/develop/SpoilerAL-winmm.dll/crt/math/fpconst.c
//
#include <float.h>
#include <math.h>

#if !CRTSYS_USE_LIBCNTPR
// Floating point used flag
const unsigned int _fltused = 0x9875;
#endif

// Floating point infinity
const double fpconst_inf = HUGE_VAL;        // 0x7FF0000000000000
const double fpconst_minus_inf = -HUGE_VAL; // 0xFFF0000000000000

// Floating point NaN
const double fpconst_nan = -NAN;    // 0x7FF8000000000000
const double fpconst_nan_ind = NAN; // 0xFFF8000000000000

// Floating point constants
const double fpconst_max = DBL_MAX;           // 0x7FEFFFFFFFFFFFFF
const double fpconst_true_min = DBL_TRUE_MIN; // 0x0000000000000001
const double fpconst_half = 0.5;              // 0x3FE0000000000000
const double fpconst_one = 1.0;               // 0x3FF0000000000000
const double fpconst_two = 2.0;               // 0x4000000000000000
const double fpconst_minus_one = -1.0;        // 0xBFF0000000000000
const double fpconst_fyl2xp1_limit = 0.29;    // 0x3FD28F5C28F5C28F

// Control word
const unsigned int fpconst_x0363 = 0x0363;
const unsigned int fpconst_x0763 = 0x0763;
const unsigned int fpconst_x0B63 = 0x0B63;
const unsigned int fpconst_x0F63 = 0x0F63;