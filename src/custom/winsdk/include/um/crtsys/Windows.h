#ifndef _CRTSYS_WINDOWS_
#define _CRTSYS_WINDOWS_

#undef _CTYPE_DISABLE_MACROS
#include <ctype.h>

#include <Ldk/Windows.h>
#include <excpt.h>
#include <stdarg.h>

// winbase.h
#include <wincontypes.h> // consoleapi.h 
#include "fileapi.h"
#include "processthreadsapi.h"
// winbase.h

#include "WinUser.h"
#include "winnls.h"

#endif // _CRTSYS_WINDOWS_