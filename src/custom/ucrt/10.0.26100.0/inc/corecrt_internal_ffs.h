//
// Windows SDK 10.0.26100.0 ucrt/inc/corecrt_internal.h includes this
// internal header, but some WDK/SDK source installations do not ship it.
// The UCRT sources consumed by crtsys do not require additional declarations
// from it, so this compatibility shim restores the expected include graph
// without changing the upstream UCRT sources.
//
#pragma once
