#include <stdio.h>
#include <stdlib.h>
#include <windows.h>

EXTERN_C_START

typedef struct _CRTSYS_MEM_BLOCK {
    SIZE_T Length;
    UCHAR Buffer[ANYSIZE_ARRAY];
} CRTSYS_MEM_BLOCK, *PCRTSYS_MEM_BLOCK;
#define CRTSYS_MEM_BLOCK_SIZE sizeof(CRTSYS_MEM_BLOCK)
#define CRTSYS_MEM_BLOCK_TAG 'MtrC'

POOL_TYPE CrtSysDefaultPoolType = PagedPool;

_Check_return_ _Ret_maybenull_ _Post_writable_byte_size_(_Size)
_ACRTIMP _CRTALLOCATOR _CRT_JIT_INTRINSIC _CRTRESTRICT _CRT_HYBRIDPATCHABLE
void* __cdecl malloc(
    _In_ _CRT_GUARDOVERFLOW size_t _Size
    )
{
    PCRTSYS_MEM_BLOCK block = (PCRTSYS_MEM_BLOCK)ExAllocatePoolWithTag( CrtSysDefaultPoolType,
                                                                        _Size + CRTSYS_MEM_BLOCK_SIZE,
                                                                        CRTSYS_MEM_BLOCK_TAG );
    if (block == NULL) {
        return NULL;
    }
    block->Length = _Size;
    return (void *)block->Buffer;
}

_Check_return_ _Ret_maybenull_ _Post_writable_byte_size_(_Count * _Size)
_ACRTIMP _CRT_JIT_INTRINSIC _CRTALLOCATOR _CRTRESTRICT _CRT_HYBRIDPATCHABLE
void* __cdecl calloc(
    _In_ _CRT_GUARDOVERFLOW size_t _Count,
    _In_ _CRT_GUARDOVERFLOW size_t _Size
    )
{
    void *buffer = malloc( _Count * _Size );
    if (buffer) {
        RtlZeroMemory(buffer, _Count * _Size);
        return buffer;
    }
    return NULL;
}

_Success_(return != 0) _Check_return_ _Ret_maybenull_ _Post_writable_byte_size_(_Size)
_ACRTIMP _CRTALLOCATOR _CRTRESTRICT _CRT_HYBRIDPATCHABLE
void* __cdecl realloc(
    _Pre_maybenull_ _Post_invalid_ void*  _Block,
    _In_ _CRT_GUARDOVERFLOW        size_t _Size
    )
{
    if (_Block == NULL) {
        return malloc(_Size);
    }

    if (_Size == 0) {
        free(_Block);
        return NULL;
    }

    PCRTSYS_MEM_BLOCK block = CONTAINING_RECORD(_Block, CRTSYS_MEM_BLOCK, Buffer);
    if (_Size <= block->Length) {
        return _Block;
    }

    void *buffer = malloc(_Size);
    if (buffer == NULL) {
        return NULL;
    }

    RtlCopyMemory(buffer, block->Buffer, block->Length);
    return buffer;
}

_Success_(return != 0) _Check_return_ _Ret_maybenull_ _Post_writable_byte_size_(_Count * _Size)
_ACRTIMP _CRTALLOCATOR _CRTRESTRICT
void* __cdecl _recalloc(
    _Pre_maybenull_ _Post_invalid_ void*  _Block,
    _In_ _CRT_GUARDOVERFLOW        size_t _Count,
    _In_ _CRT_GUARDOVERFLOW        size_t _Size
    )
{
    void* buffer = realloc(_Block, _Count * _Size);
    if (buffer) {
        RtlZeroMemory(buffer, _Count * _Size);
        return buffer;
    }
    return NULL;
}

_ACRTIMP void __cdecl free(
    _Pre_maybenull_ _Post_invalid_ void* _Block
    )
{
    if (_Block) {
        ExFreePoolWithTag( CONTAINING_RECORD(_Block, CRTSYS_MEM_BLOCK, Buffer),
                           CRTSYS_MEM_BLOCK_TAG );
    }
}

int __cdecl _callnewh(
    size_t const size
    )
{
    UNREFERENCED_PARAMETER(size);
    KdBreakPoint();
    return 0;
}
EXTERN_C_END
