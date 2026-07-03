#include <math.h>

_Check_return_ _ACRTIMP double __cdecl scalbn(_In_ double x, _In_ int n) {
  return ldexp(x, n);
}
