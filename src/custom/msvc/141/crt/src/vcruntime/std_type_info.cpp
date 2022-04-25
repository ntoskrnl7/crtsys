//
// _unDName function is not implemented yet.
//

#include <vcruntime_typeinfo.h>

EXTERN_C int __cdecl __std_type_info_compare (
    __std_type_info_data const* const lhs,
    __std_type_info_data const* const rhs
    )
{
    if (lhs == rhs)
    {
        return 0;
    }

    return strcmp(lhs->_DecoratedName + 1, rhs->_DecoratedName + 1);
}