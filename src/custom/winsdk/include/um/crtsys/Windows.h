#ifndef _CRTSYS_WINDOWS_
#define _CRTSYS_WINDOWS_

#undef _CTYPE_DISABLE_MACROS
#include <ctype.h>

#include <Ldk/Windows.h>
#include <excpt.h>
#include <stdarg.h>

#ifndef TIME_ZONE_ID_UNKNOWN
#define TIME_ZONE_ID_UNKNOWN 0
#endif
#ifndef TIME_ZONE_ID_STANDARD
#define TIME_ZONE_ID_STANDARD 1
#endif
#ifndef TIME_ZONE_ID_DAYLIGHT
#define TIME_ZONE_ID_DAYLIGHT 2
#endif

// winbase.h
#include <wincontypes.h> // consoleapi.h 
#include "WinBase.h"
#include "fileapi.h"
#include "synchapi.h"
#include "processthreadsapi.h"
// winbase.h

#include "WinUser.h"
#include "winnls.h"

#endif // _CRTSYS_WINDOWS_
