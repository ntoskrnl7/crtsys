#define _CORECRT_BUILD
#define _CRT_GLOBAL_STATE_ISOLATION
#define _CRT_DECLARE_GLOBAL_VARIABLES_DIRECTLY
#include <corecrt_internal.h>

namespace {

errno_t __cdecl crtsys_configure_narrow_program_name(
    _crt_argv_mode const mode) {
  if (mode == _crt_argv_no_arguments) {
    return 0;
  }

  _VALIDATE_RETURN_ERRCODE(mode == _crt_argv_expanded_arguments ||
                               mode == _crt_argv_unexpanded_arguments,
                           EINVAL);

  static char program_name[MAX_PATH + 1] = {};
  DWORD const length =
      GetModuleFileNameA(nullptr, program_name, static_cast<DWORD>(MAX_PATH));
  if (length == 0) {
    return EINVAL;
  }
  if (length >= MAX_PATH) {
    program_name[MAX_PATH] = '\0';
  }

  _set_pgmptr(program_name);
  return 0;
}

errno_t __cdecl crtsys_configure_wide_program_name(
    _crt_argv_mode const mode) {
  if (mode == _crt_argv_no_arguments) {
    return 0;
  }

  _VALIDATE_RETURN_ERRCODE(mode == _crt_argv_expanded_arguments ||
                               mode == _crt_argv_unexpanded_arguments,
                           EINVAL);

  static wchar_t program_name[MAX_PATH + 1] = {};
  DWORD const length =
      GetModuleFileNameW(nullptr, program_name, static_cast<DWORD>(MAX_PATH));
  if (length == 0) {
    return EINVAL;
  }
  if (length >= MAX_PATH) {
    program_name[MAX_PATH] = L'\0';
  }

  _set_wpgmptr(program_name);
  return 0;
}

} // namespace

extern "C" errno_t __cdecl _configure_narrow_argv(_crt_argv_mode const mode)
{
    return crtsys_configure_narrow_program_name(mode);
}

extern "C" errno_t __cdecl _configure_wide_argv(_crt_argv_mode const mode)
{
    return crtsys_configure_wide_program_name(mode);
}
