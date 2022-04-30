#pragma once

#include <windows.h>

#ifndef __LC_TIME_DATA
typedef struct __lc_time_data {
  const char *wday_abbr[7];
  const char *wday[7];
  const char *month_abbr[12];
  const char *month[12];
  const char *ampm[2];
  const char *ww_sdatefmt;
  const char *ww_ldatefmt;
  const char *ww_timefmt;
  int ww_caltype;
  long refcount;
  const wchar_t *_W_wday_abbr[7];
  const wchar_t *_W_wday[7];
  const wchar_t *_W_month_abbr[12];
  const wchar_t *_W_month[12];
  const wchar_t *_W_ampm[2];
  const wchar_t *_W_ww_sdatefmt;
  const wchar_t *_W_ww_ldatefmt;
  const wchar_t *_W_ww_timefmt;
  const wchar_t *_W_ww_locale_name;
} __lc_time_data;
#define __LC_TIME_DATA
#endif

#define MAX_LANG_LEN 64    /* max language name length */
#define MAX_CTRY_LEN 64    /* max country name length */
#define MAX_MODIFIER_LEN 0 /* max modifier name length - n/a */
#define MAX_LC_LEN (MAX_LANG_LEN + MAX_CTRY_LEN + MAX_MODIFIER_LEN + 3)
/* max entire locale string length */
#define MAX_CP_LEN 16   /* max code page name length */
#define CATNAMES_LEN 57 /* "LC_COLLATE=;LC_CTYPE=;..." length */

#define LC_INT_TYPE 0
#define LC_STR_TYPE 1

#ifndef _TAGLC_ID_DEFINED
typedef struct tagLC_ID {
  WORD wLanguage;
  WORD wCountry;
  WORD wCodePage;
} LC_ID, *LPLC_ID;
#define _TAGLC_ID_DEFINED
#endif /* _TAGLC_ID_DEFINED */

typedef struct tagLC_STRINGS {
  char szLanguage[MAX_LANG_LEN];
  char szCountry[MAX_CTRY_LEN];
  char szCodePage[MAX_CP_LEN];
} LC_STRINGS, *LPLC_STRINGS;

#ifndef _THREADLOCALEINFO
typedef struct threadlocaleinfostruct {
  int refcount;
  UINT lc_codepage;
  UINT lc_collate_cp;
  LCID lc_handle[6];
  LC_ID lc_id[6];
  struct {
    char *locale;
    wchar_t *wlocale;
    int *refcount;
    int *wrefcount;
  } lc_category[6];
  int lc_clike;
  int mb_cur_max;
  int *lconv_intl_refcount;
  int *lconv_num_refcount;
  int *lconv_mon_refcount;
  struct lconv *lconv;
  int *ctype1_refcount;
  unsigned short *ctype1;
  const unsigned short *pctype;
  unsigned char *pclmap;
  unsigned char *pcumap;
  struct __lc_time_data *lc_time_curr;
} threadlocinfo;
typedef threadlocinfo *pthreadlocinfo;
#define _THREADLOCALEINFO
#endif // _THREADLOCALEINFO
EXTERN_C pthreadlocinfo __ptlocinfo;

EXTERN_C struct __lc_time_data *__lc_time_curr;