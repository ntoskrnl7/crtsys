#include "../../../common/crt/crt_internal.h"

CRITICAL_SECTION CrtSyspIobEntriesLock[_IOB_ENTRIES];

#if (!CRTSYS_USE_LIBCNTPR) || CRTSYS_NEED_CRT_IOB_FUNC
EXTERN_C
_ACRTIMP_ALT FILE *__cdecl __acrt_iob_func(unsigned _Ix) {
  KdBreakPoint(); // untested :-(
  return &_iob[_Ix];
}
#endif

EXTERN_C void __cdecl _lock_file(FILE *const stream) {
  if (stream >= _iob && stream <= &_iob[_IOB_ENTRIES - 1]) {
    EnterCriticalSection(&CrtSyspIobEntriesLock[stream - _iob]);
  } else {
    KdBreakPoint();
    // untested :-(
    EnterCriticalSection(&((PCRTSYS_FILE)stream)->_lock);
  }
}

EXTERN_C void __cdecl _unlock_file(FILE *const stream) {
  if (stream >= _iob && stream <= &_iob[_IOB_ENTRIES - 1]) {
    LeaveCriticalSection(&CrtSyspIobEntriesLock[stream - _iob]);
  } else {
    KdBreakPoint();
    // untested :-(
    LeaveCriticalSection(&((PCRTSYS_FILE)stream)->_lock);
  }
}

EXTERN_C errno_t __cdecl _get_stream_buffer_pointers(FILE *const public_stream,
                                                     char ***const base,
                                                     char ***const ptr,
                                                     int **const count) {
  if (base)
    *base = &public_stream->_base;

  if (ptr)
    *ptr = &public_stream->_ptr;

  if (count)
    *count = &public_stream->_cnt;

  return 0;
}
