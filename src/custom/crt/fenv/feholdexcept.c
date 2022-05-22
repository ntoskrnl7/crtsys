#include <fenv.h>

_ACRTIMP _Success_(return == 0) int __cdecl feholdexcept(_Out_ fenv_t *_Env)
{
	fegetenv(_Env);
	feclearexcept(FE_ALL_EXCEPT);
	return 0;
}