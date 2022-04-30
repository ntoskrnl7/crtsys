#include <windows.h>

//
// fltintrn.h
//
typedef struct _flt {
  int flags;
  int nbytes; /* number of characters read */
  long lval;
  double dval; /* the returned floating point number */
} * FLT;

//
// :-(
//
// untested
//

#pragma warning(disable : 4100)

FLT __cdecl _fltin(const char *str, int len_ignore, int scale_ignore,
                   int radix_ignore) {
  KdBreakPoint();
  return NULL;
}