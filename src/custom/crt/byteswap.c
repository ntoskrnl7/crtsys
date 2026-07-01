#include <stdlib.h>

#pragma function(_byteswap_ushort, _byteswap_ulong, _byteswap_uint64)

unsigned short __cdecl _byteswap_ushort(unsigned short value) {
  return (unsigned short)((value << 8) | (value >> 8));
}

unsigned long __cdecl _byteswap_ulong(unsigned long value) {
  return ((value & 0x000000FFul) << 24) | ((value & 0x0000FF00ul) << 8) |
         ((value & 0x00FF0000ul) >> 8) | ((value & 0xFF000000ul) >> 24);
}

unsigned __int64 __cdecl _byteswap_uint64(unsigned __int64 value) {
  return ((value & 0x00000000000000FFui64) << 56) |
         ((value & 0x000000000000FF00ui64) << 40) |
         ((value & 0x0000000000FF0000ui64) << 24) |
         ((value & 0x00000000FF000000ui64) << 8) |
         ((value & 0x000000FF00000000ui64) >> 8) |
         ((value & 0x0000FF0000000000ui64) >> 24) |
         ((value & 0x00FF000000000000ui64) >> 40) |
         ((value & 0xFF00000000000000ui64) >> 56);
}
