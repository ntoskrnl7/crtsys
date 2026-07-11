#pragma once

// raw_pdb is parsed as part of crtsys' kernel-mode stacktrace ABI. Keep this
// shim scoped to raw_pdb translation units only: WDK headers define short
// names such as None/AMD64 that collide with CodeView enum members, and
// raw_pdb's Debug assert path intentionally breaks into the debugger.
#ifdef _DEBUG
#undef _DEBUG
#endif

#ifndef NDEBUG
#define NDEBUG
#endif

#ifdef None
#undef None
#endif

#ifdef Register
#undef Register
#endif

#ifdef AMD64
#undef AMD64
#endif

#ifdef ARM
#undef ARM
#endif

#ifdef ARM64
#undef ARM64
#endif

#ifdef min
#undef min
#endif

#ifdef max
#undef max
#endif
