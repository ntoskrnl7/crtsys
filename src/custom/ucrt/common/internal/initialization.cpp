#include <corecrt_startup.h>
#include <stdio.h>
#include <stdlib.h>
#include <vcruntime_internal.h>

EXTERN_C_START

extern _onexit_table_t __acrt_atexit_table;
extern _onexit_table_t __acrt_at_quick_exit_table;

static bool __cdecl initialize_c() {
  _initialize_onexit_table(&__acrt_atexit_table);
  _initialize_onexit_table(&__acrt_at_quick_exit_table);

  // Do C initialization:
  if (_initterm_e(__xi_a, __xi_z) != 0) {
    return false;
  }

  // Do C++ initialization:
  _initterm(__xc_a, __xc_z);
  return true;
}

static bool __cdecl uninitialize_c(bool) {
  // Do pre-termination:
  _initterm(__xp_a, __xp_z);

  // Do termination:
  _initterm(__xt_a, __xt_z);
  return true;
}

__crt_bool __cdecl __acrt_initialize() {
  __isa_available_init();
  return initialize_c();
}

__crt_bool __cdecl __acrt_uninitialize(__crt_bool const terminating) {
  UNREFERENCED_PARAMETER(terminating);
  return uninitialize_c(false);
}
EXTERN_C_END