#ifndef _FILE_DEFINED
//
// 10.0.17763.0
// libcntpr.lib _iobuf는 _lock이 없는 순수 _iobuf구조입니다.
//
struct _iobuf {
  char *_ptr;
  int _cnt;
  char *_base;
  int _flag;
  int _file;
  int _charbuf;
  int _bufsiz;
  char *_tmpfname;
};
typedef struct _iobuf FILE;
#define _FILE_DEFINED
#endif /* _FILE_DEFINED */

#include <stdio.h>
#include <windows.h>

EXTERN_C FILE _iob[_IOB_ENTRIES];

typedef struct _CRTSYS_FILE {
    FILE _file;
    CRITICAL_SECTION _lock;
} CRTSYS_FILE, *PCRTSYS_FILE;

extern CRITICAL_SECTION CrtSyspIobEntriesLock[_IOB_ENTRIES];