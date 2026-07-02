.386
.model flat

EXTERN _InitOnceBeginInitialize@16:PROC
EXTERN _InitOnceComplete@12:PROC

.data

; MSVC STL x86 emits imported stdcall ABI helper references for std::call_once.
; LDK provides the underlying Win32 InitOnce functions as static-library
; symbols, so publish the import-pointer data symbols that the STL expects.
PUBLIC __imp____std_init_once_begin_initialize@16
__imp____std_init_once_begin_initialize@16 DD OFFSET _InitOnceBeginInitialize@16

PUBLIC __imp____std_init_once_complete@12
__imp____std_init_once_complete@12 DD OFFSET _InitOnceComplete@12

END
