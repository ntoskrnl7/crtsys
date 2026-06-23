//
// _unDName function is not implemented yet.
//

#include <vcruntime_typeinfo.h>

static char const* __cdecl crtsys_type_info_name(__std_type_info_data const* const data)
{
    char const* const decorated = data->_DecoratedName[0] == '.'
        ? data->_DecoratedName + 1
        : data->_DecoratedName;

    if (strcmp(decorated, "_N") == 0) return "bool";
    if (strcmp(decorated, "D") == 0) return "char";
    if (strcmp(decorated, "C") == 0) return "signed char";
    if (strcmp(decorated, "E") == 0) return "unsigned char";
    if (strcmp(decorated, "F") == 0) return "short";
    if (strcmp(decorated, "G") == 0) return "unsigned short";
    if (strcmp(decorated, "H") == 0) return "int";
    if (strcmp(decorated, "I") == 0) return "unsigned int";
    if (strcmp(decorated, "J") == 0) return "long";
    if (strcmp(decorated, "K") == 0) return "unsigned long";
    if (strcmp(decorated, "_J") == 0) return "__int64";
    if (strcmp(decorated, "_K") == 0) return "unsigned __int64";
    if (strcmp(decorated, "M") == 0) return "float";
    if (strcmp(decorated, "N") == 0) return "double";
    if (strcmp(decorated, "O") == 0) return "long double";
    if (strcmp(decorated, "X") == 0) return "void";
    return decorated;
}

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

EXTERN_C size_t __cdecl __std_type_info_hash(
    __std_type_info_data const* const data
    )
{
#ifdef _WIN64
    size_t value = 14695981039346656037ULL;
    size_t const prime = 1099511628211ULL;
#else
    size_t value = 2166136261U;
    size_t const prime = 16777619U;
#endif

    for (char const* it = data->_DecoratedName + 1; *it != '\0'; ++it)
    {
        value ^= static_cast<size_t>(static_cast<unsigned char>(*it));
        value *= prime;
    }

#ifdef _WIN64
    value ^= value >> 32;
#endif
    return value;
}

EXTERN_C char const* __cdecl __std_type_info_name(
    __std_type_info_data* const data,
    __type_info_node* const
    )
{
    if (data->_UndecoratedName)
    {
        return data->_UndecoratedName;
    }

    data->_UndecoratedName = crtsys_type_info_name(data);
    return data->_UndecoratedName;
}

EXTERN_C void __cdecl __std_type_info_destroy_list(
    __type_info_node* const
    )
{
}
