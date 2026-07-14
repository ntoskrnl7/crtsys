//
// Windows SDK UCRT source versions 10.0.26100.0 and later include this
// internal header from corecrt_internal.h, but some SDK source packages do
// not ship it. The UCRT sources consumed by crtsys require no declarations
// from the missing file, so this compatibility shim restores the upstream
// include graph without modifying the installed Windows SDK sources.
//
#pragma once
