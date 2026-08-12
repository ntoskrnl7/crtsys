cmake_minimum_required(VERSION 3.14)

cmake_policy(SET CMP0021 NEW)
if(POLICY CMP0091)
  cmake_policy(SET CMP0091 NEW)
endif()

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake/")
include(pkg-utils)

pkg_get_version(CRTSYS ${CMAKE_CURRENT_SOURCE_DIR}/include/.internal/version VERSION)

project(
    crtsys
    VERSION "${VERSION}"
    DESCRIPTION "C Runtime Lbrary for Windows driver file (*.sys)"
    HOMEPAGE_URL "https://github.com/ntoskrnl7/crtsys"
    LANGUAGES C CXX ASM_MASM
    )

if(CMAKE_CURRENT_SOURCE_DIR STREQUAL CMAKE_SOURCE_DIR)
  set(CRTSYS_IS_TOP_LEVEL ON)
else()
  set(CRTSYS_IS_TOP_LEVEL OFF)
endif()

option(CRTSYS_NTL_MAIN      "Set to ON to use ntl::main" ON)
option(CRTSYS_USE_LIBCNTPR  "Set to ON to use libcntpr" ON)
option(CRTSYS_ENABLE_DIAGNOSTIC_BREAKPOINTS "Set to ON to enable crtsys diagnostic KdBreakPoint calls" OFF)
option(CRTSYS_ENABLE_INSTALL "Enable crtsys install rules" ${CRTSYS_IS_TOP_LEVEL})
option(CRTSYS_BUILD_NTL_KERNEL_CONTENT_CODECS
  "Build the optional PASSIVE_LEVEL NTL kernel zlib/Brotli libraries" OFF)
set(CRTSYS_LDK_SOURCE_DIR "" CACHE PATH "Use a local LDK source tree instead of fetching the default LDK release")
set(CRTSYS_RAW_PDB_SOURCE_DIR "" CACHE PATH "Use a local raw_pdb source tree instead of fetching the pinned revision")
set(CRTSYS_UCXXRT_SOURCE_DIR "" CACHE PATH "Use a local ucxxrt source tree instead of fetching the pinned revision")
set(CRTSYS_FINDWDK_SOURCE_DIR
  "${CMAKE_CURRENT_SOURCE_DIR}/cmake/vendor/findwdk" CACHE PATH
  "Use a local FindWDK source tree; defaults to the pinned bundled revision")
set(CRTSYS_TARGET_ARCHITECTURE "" CACHE STRING
  "Target architecture override for non-Visual Studio generators: x86, x64, ARM, or ARM64")

function(crtsys_resolve_target_architecture out_var)
  if(NOT "${CRTSYS_TARGET_ARCHITECTURE}" STREQUAL "")
    set(_architecture "${CRTSYS_TARGET_ARCHITECTURE}")
  elseif(NOT "${CMAKE_VS_PLATFORM_NAME}" STREQUAL "")
    set(_architecture "${CMAKE_VS_PLATFORM_NAME}")
  elseif(NOT "${CMAKE_GENERATOR_PLATFORM}" STREQUAL "")
    string(REGEX REPLACE ",.*$" "" _architecture
      "${CMAKE_GENERATOR_PLATFORM}")
  elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "^(ARM64|arm64|aarch64)$")
    set(_architecture "ARM64")
  elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "^(ARM|arm)$")
    set(_architecture "ARM")
  elseif(CMAKE_SIZEOF_VOID_P EQUAL 8)
    set(_architecture "x64")
  elseif(CMAKE_SIZEOF_VOID_P EQUAL 4)
    set(_architecture "x86")
  else()
    message(FATAL_ERROR
      "Unable to determine the crtsys target architecture. Set "
      "CRTSYS_TARGET_ARCHITECTURE to x86, x64, ARM, or ARM64.")
  endif()

  string(TOLOWER "${_architecture}" _architecture_lower)
  if(_architecture_lower STREQUAL "win32" OR
     _architecture_lower STREQUAL "x86")
    set(_architecture "x86")
  elseif(_architecture_lower STREQUAL "x64" OR
         _architecture_lower STREQUAL "amd64")
    set(_architecture "x64")
  elseif(_architecture_lower STREQUAL "arm")
    set(_architecture "ARM")
  elseif(_architecture_lower STREQUAL "arm64" OR
         _architecture_lower STREQUAL "aarch64")
    set(_architecture "ARM64")
  else()
    message(FATAL_ERROR
      "Unsupported crtsys target architecture: ${_architecture}")
  endif()

  set(${out_var} "${_architecture}" PARENT_SCOPE)
endfunction()

crtsys_resolve_target_architecture(CRTSYS_NATIVE_ARCH)

# A source-tree consumer may intentionally link the prebuilt native archives.
# In that mode CPM still enters this directory to expose crtsys_SOURCE_DIR, but
# configuring the source targets would fetch dependencies and put crtsys back
# into the consumer's build graph.  Leave the dependency directory target-free
# so every consumer can reuse the archive prepared by CI.
if(NOT CRTSYS_IS_TOP_LEVEL AND CRTSYS_USE_PREBUILT)
  message(STATUS "crtsys source targets disabled; using prebuilt native archives")
  return()
endif()

function(crtsys_scope_compile_options_to_c_cxx TARGET_NAME)
  get_target_property(TARGET_COMPILE_OPTIONS ${TARGET_NAME} COMPILE_OPTIONS)
  if(NOT TARGET_COMPILE_OPTIONS)
    return()
  endif()

  set(SCOPED_COMPILE_OPTIONS)
  foreach(COMPILE_OPTION IN LISTS TARGET_COMPILE_OPTIONS)
    list(APPEND SCOPED_COMPILE_OPTIONS "$<$<COMPILE_LANGUAGE:C,CXX>:${COMPILE_OPTION}>")
  endforeach()

  set_target_properties(${TARGET_NAME} PROPERTIES COMPILE_OPTIONS "${SCOPED_COMPILE_OPTIONS}")
endfunction()

function(crtsys_get_msvc_sdk_include_dirs OUT_VAR)
  set(INCLUDE_DIRS)

  if(MSVC AND CMAKE_CXX_COMPILER)
    get_filename_component(MSVC_COMPILER_ARCH_DIR "${CMAKE_CXX_COMPILER}" DIRECTORY)
    get_filename_component(MSVC_COMPILER_HOST_DIR "${MSVC_COMPILER_ARCH_DIR}" DIRECTORY)
    get_filename_component(MSVC_COMPILER_BIN_DIR "${MSVC_COMPILER_HOST_DIR}" DIRECTORY)
    get_filename_component(MSVC_TOOLS_DIR "${MSVC_COMPILER_BIN_DIR}" DIRECTORY)
    get_filename_component(MSVC_VC_DIR "${MSVC_TOOLS_DIR}/../../.." ABSOLUTE)

    if(EXISTS "${MSVC_TOOLS_DIR}/include")
      list(APPEND INCLUDE_DIRS "${MSVC_TOOLS_DIR}/include")
    endif()

    if(EXISTS "${MSVC_VC_DIR}/Auxiliary/VS/include")
      list(APPEND INCLUDE_DIRS "${MSVC_VC_DIR}/Auxiliary/VS/include")
    endif()
  endif()

  if(DEFINED WDK_ROOT AND DEFINED CMAKE_VS_WINDOWS_TARGET_PLATFORM_VERSION)
    foreach(SDK_INCLUDE_KIND ucrt um shared winrt cppwinrt)
      set(SDK_INCLUDE_DIR "${WDK_ROOT}/Include/${CMAKE_VS_WINDOWS_TARGET_PLATFORM_VERSION}/${SDK_INCLUDE_KIND}")
      if(EXISTS "${SDK_INCLUDE_DIR}")
        list(APPEND INCLUDE_DIRS "${SDK_INCLUDE_DIR}")
      endif()
    endforeach()
  endif()

  set(${OUT_VAR} "${INCLUDE_DIRS}" PARENT_SCOPE)
endfunction()

function(crtsys_generate_msvc_future_overlay OUT_VAR)
  set(${OUT_VAR} "" PARENT_SCOPE)

  if(NOT MSVC OR NOT CMAKE_CXX_COMPILER)
    return()
  endif()

  get_filename_component(MSVC_COMPILER_ARCH_DIR "${CMAKE_CXX_COMPILER}" DIRECTORY)
  get_filename_component(MSVC_COMPILER_HOST_DIR "${MSVC_COMPILER_ARCH_DIR}" DIRECTORY)
  get_filename_component(MSVC_COMPILER_BIN_DIR "${MSVC_COMPILER_HOST_DIR}" DIRECTORY)
  get_filename_component(MSVC_TOOLS_DIR "${MSVC_COMPILER_BIN_DIR}" DIRECTORY)
  set(MSVC_FUTURE_HEADER "${MSVC_TOOLS_DIR}/include/future")
  if(NOT EXISTS "${MSVC_FUTURE_HEADER}")
    return()
  endif()

  file(READ "${MSVC_FUTURE_HEADER}" CRTSYS_MSVC_FUTURE_CONTENT)

  set(CRTSYS_FUTURE_IS_READY_NOEXCEPT_SNIPPET
"    bool _Is_ready() const noexcept {
        return _State._Is_ready();
    }

")
  set(CRTSYS_FUTURE_READY_OR_STORED_NOEXCEPT_SNIPPET
"    bool _Is_ready() const noexcept {
        return _State._Is_ready();
    }

    bool _Already_has_stored_result() const noexcept {
        return _State._Ptr() && _State._Ptr()->_Already_has_stored_result();
    }

")
  set(CRTSYS_FUTURE_IS_READY_SNIPPET
"    bool _Is_ready() const {
        return _State._Is_ready();
    }

")
  set(CRTSYS_FUTURE_READY_OR_STORED_SNIPPET
"    bool _Is_ready() const {
        return _State._Is_ready();
    }

    bool _Already_has_stored_result() const {
        return _State._Ptr() && _State._Ptr()->_Already_has_stored_result();
    }

")

  set(CRTSYS_FUTURE_IS_READY_INDEX -1)
  string(FIND "${CRTSYS_MSVC_FUTURE_CONTENT}" "${CRTSYS_FUTURE_IS_READY_NOEXCEPT_SNIPPET}" CRTSYS_FUTURE_IS_READY_NOEXCEPT_INDEX)
  if(NOT CRTSYS_FUTURE_IS_READY_NOEXCEPT_INDEX EQUAL -1)
    string(REPLACE "${CRTSYS_FUTURE_IS_READY_NOEXCEPT_SNIPPET}" "${CRTSYS_FUTURE_READY_OR_STORED_NOEXCEPT_SNIPPET}" CRTSYS_MSVC_FUTURE_CONTENT "${CRTSYS_MSVC_FUTURE_CONTENT}")
  else()
    string(FIND "${CRTSYS_MSVC_FUTURE_CONTENT}" "${CRTSYS_FUTURE_IS_READY_SNIPPET}" CRTSYS_FUTURE_IS_READY_INDEX)
  endif()
  if(CRTSYS_FUTURE_IS_READY_NOEXCEPT_INDEX EQUAL -1 AND CRTSYS_FUTURE_IS_READY_INDEX EQUAL -1)
    message(WARNING "Unable to patch MSVC <future>: _Promise::_Is_ready shape was not recognized.")
    return()
  endif()
  if(NOT CRTSYS_FUTURE_IS_READY_INDEX EQUAL -1)
    string(REPLACE "${CRTSYS_FUTURE_IS_READY_SNIPPET}" "${CRTSYS_FUTURE_READY_OR_STORED_SNIPPET}" CRTSYS_MSVC_FUTURE_CONTENT "${CRTSYS_MSVC_FUTURE_CONTENT}")
  endif()

  set(CRTSYS_FUTURE_PROMISE_DTOR_CONDITION "if (_MyPromise._Is_valid() && !_MyPromise._Is_ready())")
  set(CRTSYS_FUTURE_PROMISE_DTOR_AT_THREAD_EXIT_CONDITION "if (_MyPromise._Is_valid() && !_MyPromise._Is_ready() && !_MyPromise._Is_ready_at_thread_exit())")
  set(CRTSYS_FUTURE_DTOR_INDEX -1)
  set(CRTSYS_FUTURE_DTOR_AT_THREAD_EXIT_INDEX -1)
  string(FIND "${CRTSYS_MSVC_FUTURE_CONTENT}" "${CRTSYS_FUTURE_PROMISE_DTOR_CONDITION}" CRTSYS_FUTURE_DTOR_INDEX)
  string(FIND "${CRTSYS_MSVC_FUTURE_CONTENT}" "${CRTSYS_FUTURE_PROMISE_DTOR_AT_THREAD_EXIT_CONDITION}" CRTSYS_FUTURE_DTOR_AT_THREAD_EXIT_INDEX)
  if(CRTSYS_FUTURE_DTOR_INDEX EQUAL -1 AND CRTSYS_FUTURE_DTOR_AT_THREAD_EXIT_INDEX EQUAL -1)
    message(WARNING "Unable to patch MSVC <future>: promise destructor shape was not recognized.")
    return()
  endif()

  # MSVC STL keeps set_value_at_thread_exit() states unready until the CRT
  # thread-exit callback broadcasts. A promise destructor must not translate
  # that already-stored-but-not-yet-ready value into broken_promise.
  if(NOT CRTSYS_FUTURE_DTOR_AT_THREAD_EXIT_INDEX EQUAL -1)
    string(REPLACE
      "${CRTSYS_FUTURE_PROMISE_DTOR_AT_THREAD_EXIT_CONDITION}"
      "if (_MyPromise._Is_valid() && !_MyPromise._Is_ready() && !_MyPromise._Is_ready_at_thread_exit() && !_MyPromise._Already_has_stored_result())"
      CRTSYS_MSVC_FUTURE_CONTENT
      "${CRTSYS_MSVC_FUTURE_CONTENT}")
  endif()
  if(NOT CRTSYS_FUTURE_DTOR_INDEX EQUAL -1)
    string(REPLACE
      "${CRTSYS_FUTURE_PROMISE_DTOR_CONDITION}"
      "if (_MyPromise._Is_valid() && !_MyPromise._Is_ready() && !_MyPromise._Already_has_stored_result())"
      CRTSYS_MSVC_FUTURE_CONTENT
      "${CRTSYS_MSVC_FUTURE_CONTENT}")
  endif()

  set(CRTSYS_MSVC_FUTURE_OVERLAY_DIR "${CMAKE_CURRENT_BINARY_DIR}/crtsys-msvc-overlay/${MSVC_TOOLSET_VERSION}")
  file(MAKE_DIRECTORY "${CRTSYS_MSVC_FUTURE_OVERLAY_DIR}")
  file(WRITE "${CRTSYS_MSVC_FUTURE_OVERLAY_DIR}/future" "${CRTSYS_MSVC_FUTURE_CONTENT}")
  set(${OUT_VAR} "${CRTSYS_MSVC_FUTURE_OVERLAY_DIR}" PARENT_SCOPE)
endfunction()

# x64 is always set to use libcntpr. (_NLG_Notify, _NLG_Return2)
if(CRTSYS_NATIVE_ARCH STREQUAL "x64")
  set(CRTSYS_USE_LIBCNTPR ON)
endif()

set(CRTSYS_MSVC_COMPAT_TOOLSET_VERSION "${MSVC_TOOLSET_VERSION}")
if(MSVC_TOOLSET_VERSION GREATER 143)
  set(CRTSYS_MSVC_COMPAT_TOOLSET_VERSION 143)
endif()

if(MSVC_TOOLSET_VERSION GREATER_EQUAL 142)
  set(UCXXRT_ENABLED OFF)
else()
  set(UCXXRT_ENABLED ON)
endif()

include(cmake/CPM.cmake)

if(NOT EXISTS "${CRTSYS_FINDWDK_SOURCE_DIR}/cmake/FindWdk.cmake")
  message(FATAL_ERROR
    "CRTSYS_FINDWDK_SOURCE_DIR does not contain cmake/FindWdk.cmake: "
    "${CRTSYS_FINDWDK_SOURCE_DIR}")
endif()
CPMAddPackage(
  NAME FindWDK
  SOURCE_DIR "${CRTSYS_FINDWDK_SOURCE_DIR}"
  DOWNLOAD_ONLY YES
)

if(CRTSYS_NATIVE_ARCH STREQUAL "x64" AND
    NOT CRTSYS_USE_LIBCNTPR)
  set(LDK_USE_RAISE_EXCEPTION ON CACHE BOOL
      "Set to ON to use ldk!RaiseException" FORCE)
  set(LDK_USE_RTL_RAISE_EXCEPTION ON CACHE BOOL
      "Set to ON to use ldk!RtlRaiseException" FORCE)
else()
  # libcntpr already supplies RaiseException and imports RtlRaiseException
  # from the kernel. Building the Ldk definitions as well creates archive
  # member collisions and previously forced every consuming driver to use
  # /FORCE:MULTIPLE.
  set(LDK_USE_RAISE_EXCEPTION OFF CACHE BOOL
      "Set to ON to use ldk!RaiseException" FORCE)
  set(LDK_USE_RTL_RAISE_EXCEPTION OFF CACHE BOOL
      "Set to ON to use ldk!RtlRaiseException" FORCE)
endif()
if (NOT CRTSYS_ENABLE_DIAGNOSTIC_BREAKPOINTS)
  set(LDK_ENABLE_DIAGNOSTIC_BREAKPOINTS OFF CACHE BOOL "Set to OFF to suppress LDK diagnostic KdBreakPoint calls")
endif()
if(CRTSYS_LDK_SOURCE_DIR)
  CPMAddPackage(NAME Ldk SOURCE_DIR "${CRTSYS_LDK_SOURCE_DIR}")
else()
  CPMAddPackage("gh:ntoskrnl7/Ldk@0.7.25")
endif()
function(crtsys_set_kernel_static_msvc_runtime target_name)
  if(NOT TARGET "${target_name}")
    message(FATAL_ERROR "Expected CMake target was not defined: ${target_name}")
  endif()

  if(POLICY CMP0091)
    set_property(
      TARGET "${target_name}"
      PROPERTY MSVC_RUNTIME_LIBRARY
        "MultiThreaded"
    )
  endif()

  # The Visual Studio property sheets used to compile the CRT implementation
  # deliberately clear RuntimeLibrary. Keep an explicit private /MT after
  # those imports so the CRT headers do not see _DLL and mark the definitions
  # in this static archive as dllimport. Kernel consumers must use the same
  # static runtime selection, so /MT remains part of the public build contract.
  # This also keeps CMake 3.14 working.
  target_compile_options(
    "${target_name}" PUBLIC
    "$<$<COMPILE_LANGUAGE:C,CXX>:/MT>"
  )

  # Kernel libraries must not inject a user-mode CRT default library into the
  # consuming driver. They provide the required CRT surface themselves and
  # the WDK supplies the remaining kernel imports.
  target_compile_options(
    "${target_name}" PRIVATE
    "$<$<COMPILE_LANGUAGE:C,CXX>:/Zl>"
  )
endfunction()

crtsys_set_kernel_static_msvc_runtime(Ldk)
if(CRTSYS_RAW_PDB_SOURCE_DIR)
  CPMAddPackage(
    NAME raw_pdb
    SOURCE_DIR "${CRTSYS_RAW_PDB_SOURCE_DIR}"
    DOWNLOAD_ONLY YES
  )
else()
  CPMAddPackage(
    NAME raw_pdb
    GITHUB_REPOSITORY MolecularMatters/raw_pdb
    GIT_TAG 43cc59b7037238ad84e1d5abe10711a06d998fa0
    DOWNLOAD_ONLY YES
  )
endif()

# FindWDK is bundled at a pinned revision so source builds and installed
# consumers use the same reproducible WDK discovery implementation.
find_package(WDK REQUIRED)
crtsys_get_msvc_sdk_include_dirs(CRTSYS_MSVC_SDK_INCLUDE_DIRS)
crtsys_generate_msvc_future_overlay(CRTSYS_MSVC_FUTURE_OVERLAY_DIR)

#
# Add source files.
#

file(GLOB SOURCE_FILES CONFIGURE_DEPENDS
  src/*.cpp

  # misc
  src/custom/misc/*.cpp
  src/custom/misc/*.c

  # crt
  src/custom/crt/*.c
  src/custom/crt/fenv/*.c

  # ucrt
  src/custom/ucrt/common/*.cpp
  src/custom/ucrt/common/*/*.cpp

  # ucrt
  src/custom/ucrt/${CMAKE_VS_WINDOWS_TARGET_PLATFORM_VERSION}/*.cpp
  src/custom/ucrt/${CMAKE_VS_WINDOWS_TARGET_PLATFORM_VERSION}/*/*.cpp
  )

file(GLOB RAW_PDB_SOURCE_FILES
  ${raw_pdb_SOURCE_DIR}/src/*.cpp
  )
list(APPEND SOURCE_FILES ${RAW_PDB_SOURCE_FILES})
set_source_files_properties(
  ${RAW_PDB_SOURCE_FILES}
  PROPERTIES
    COMPILE_OPTIONS "$<$<COMPILE_LANGUAGE:CXX>:/FI${CMAKE_CURRENT_SOURCE_DIR}/src/custom/misc/raw_pdb_kernel_compat.h>"
)

if(CRTSYS_NATIVE_ARCH STREQUAL "x86")
  file(GLOB MATH_SOURCE_FILES
    src/custom/crt/math/*.c
    )
  list(APPEND SOURCE_FILES ${MATH_SOURCE_FILES})
  list(APPEND SOURCE_FILES src/custom/crt/tls_x86.asm)
  list(APPEND SOURCE_FILES src/custom/misc/stl_init_once_abi_x86.asm)
else()
  file(GLOB MATH_SOURCE_FILES
  src/custom/crt/math/nextafter.c
  src/custom/crt/math/fpclassify.c
  src/custom/crt/math/fma.c
  src/custom/crt/math/copysign.c
  src/custom/crt/math/ilogb.c
  src/custom/crt/math/log2.c
  src/custom/crt/math/scalbn.c
  )
  list(APPEND SOURCE_FILES ${MATH_SOURCE_FILES})
endif()

if (UCXXRT_ENABLED)
  list(APPEND SOURCE_FILES src/custom/msvc/common/crt/src/stl/winapisupp.cpp)
  list(REMOVE_ITEM SOURCE_FILES ${CMAKE_CURRENT_SOURCE_DIR}/src/custom/crt/math/ftol3.c)

  if(CRTSYS_UCXXRT_SOURCE_DIR)
    CPMAddPackage(
      NAME ucxxrt
      SOURCE_DIR "${CRTSYS_UCXXRT_SOURCE_DIR}"
      DOWNLOAD_ONLY YES
    )
  else()
    CPMAddPackage(
      NAME ucxxrt
      GITHUB_REPOSITORY ntoskrnl7/ucxxrt
      GIT_TAG cfb00b2ad3595fa1f89a3b3a665fa742cf5aa1aa
      DOWNLOAD_ONLY YES
    )
  endif()

  file(GLOB_RECURSE UCXXRT_SOURCE_FILES
    ${ucxxrt_SOURCE_DIR}/src/*.cpp
    ${ucxxrt_SOURCE_DIR}/vcruntime/*.cpp
    ${ucxxrt_SOURCE_DIR}/misc/*.cpp
    )
  list(REMOVE_ITEM UCXXRT_SOURCE_FILES ${ucxxrt_SOURCE_DIR}/src/vcruntime/crt.cpp)
  list(REMOVE_ITEM UCXXRT_SOURCE_FILES ${ucxxrt_SOURCE_DIR}/src/vcruntime/locales.cpp)
  list(REMOVE_ITEM UCXXRT_SOURCE_FILES ${ucxxrt_SOURCE_DIR}/src/vcruntime/thread.cpp)
  if(NOT CRTSYS_NATIVE_ARCH STREQUAL "x86" AND
     NOT CRTSYS_NATIVE_ARCH STREQUAL "x64")
    list(REMOVE_ITEM UCXXRT_SOURCE_FILES ${ucxxrt_SOURCE_DIR}/src/vcruntime/x86_exception_filter.cpp)
  endif()
  list(REMOVE_ITEM UCXXRT_SOURCE_FILES ${ucxxrt_SOURCE_DIR}/src/cpu_disp.cpp)
  list(REMOVE_ITEM UCXXRT_SOURCE_FILES ${ucxxrt_SOURCE_DIR}/src/terminate.cpp)
  list(REMOVE_ITEM UCXXRT_SOURCE_FILES ${ucxxrt_SOURCE_DIR}/src/misc/invalid_parameter.cpp)
  list(REMOVE_ITEM UCXXRT_SOURCE_FILES ${ucxxrt_SOURCE_DIR}/src/sys_main.cpp)
  list(REMOVE_ITEM UCXXRT_SOURCE_FILES ${ucxxrt_SOURCE_DIR}/src/unittest.cpp)
  if(CRTSYS_NATIVE_ARCH STREQUAL "x64")
    file(GLOB PLATFORM_SOURCE_FILES
      ${ucxxrt_SOURCE_DIR}/src/x64/*.cpp
      ${ucxxrt_SOURCE_DIR}/src/x64/*.asm
    )
  elseif(CRTSYS_NATIVE_ARCH STREQUAL "x86")
    file(GLOB PLATFORM_SOURCE_FILES
        ${ucxxrt_SOURCE_DIR}/src/i386/*.cpp
        ${ucxxrt_SOURCE_DIR}/src/i386/*.asm
        )
  endif()
  list(APPEND UCXXRT_SOURCE_FILES ${PLATFORM_SOURCE_FILES})

  list(APPEND SOURCE_FILES ${UCXXRT_SOURCE_FILES})
endif()

wdk_add_library(
    crtsys
    EXTENDED_CPP_FEATURES
    WINVER 0x0602
    STATIC
    ${SOURCE_FILES}
    )
crtsys_scope_compile_options_to_c_cxx(crtsys)

#
# Add include directories
#
get_target_property(INC_DIR_TMP crtsys INCLUDE_DIRECTORIES)
if (UCXXRT_ENABLED)
  set(CRTSYS_INCLUDE_DIRS
    ${CRTSYS_MSVC_FUTURE_OVERLAY_DIR}
    "${CMAKE_CURRENT_SOURCE_DIR}/src/custom/winsdk/include/um/crtsys"
    "${Ldk_SOURCE_DIR}/include/Ldk"
    "${ucxxrt_SOURCE_DIR}/src"
    "${ucxxrt_SOURCE_DIR}/src/vcruntime"
    ${CRTSYS_MSVC_SDK_INCLUDE_DIRS}
    ${INC_DIR_TMP}
  )
  set_property(TARGET crtsys PROPERTY INCLUDE_DIRECTORIES ${CRTSYS_INCLUDE_DIRS})
  target_include_directories(crtsys PRIVATE "${ucxxrt_SOURCE_DIR}")
else()
  set(CRTSYS_INCLUDE_DIRS
    ${CRTSYS_MSVC_FUTURE_OVERLAY_DIR}
    "${CMAKE_CURRENT_SOURCE_DIR}/src/custom/winsdk/include/um/crtsys"
    "${Ldk_SOURCE_DIR}/include/Ldk"
    ${CRTSYS_MSVC_SDK_INCLUDE_DIRS}
    ${INC_DIR_TMP}
  )
  set_property(TARGET crtsys PROPERTY INCLUDE_DIRECTORIES ${CRTSYS_INCLUDE_DIRS})
endif()

target_include_directories(crtsys PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src/custom/msvc/${CRTSYS_MSVC_COMPAT_TOOLSET_VERSION}/include")
target_include_directories(crtsys PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src/custom/msvc/${CRTSYS_MSVC_COMPAT_TOOLSET_VERSION}/crt/src/vcruntime")
target_include_directories(crtsys PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src/custom/msvc/${CRTSYS_MSVC_COMPAT_TOOLSET_VERSION}/crt/src/stl")
target_include_directories(crtsys BEFORE PRIVATE
  "${CMAKE_CURRENT_SOURCE_DIR}/src/custom/ucrt/${CMAKE_VS_WINDOWS_TARGET_PLATFORM_VERSION}/inc"
  "${CMAKE_CURRENT_SOURCE_DIR}/src/custom/ucrt/common/inc"
)
target_include_directories(crtsys PRIVATE "${raw_pdb_SOURCE_DIR}/src")
target_include_directories(crtsys PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/include")

# The optional NTL KMDF entry object is part of crtsys.lib. Build that object
# against the oldest KMDF contract supported by crtsys; a consuming driver may
# still select a newer KMDF version for its own sources and WDF import library.
set(CRTSYS_MINIMUM_KMDF_VERSION "1.15")
set(CRTSYS_MINIMUM_KMDF_INCLUDE_DIR
    "${WDK_ROOT}/Include/wdf/kmdf/${CRTSYS_MINIMUM_KMDF_VERSION}")
if(NOT EXISTS "${CRTSYS_MINIMUM_KMDF_INCLUDE_DIR}/wdf.h")
  message(FATAL_ERROR
    "KMDF ${CRTSYS_MINIMUM_KMDF_VERSION} headers were not found at "
    "${CRTSYS_MINIMUM_KMDF_INCLUDE_DIR}. Install the matching WDK KMDF headers.")
endif()
target_include_directories(crtsys PRIVATE "${CRTSYS_MINIMUM_KMDF_INCLUDE_DIR}")

#
# link libraries
#
target_link_libraries(crtsys PRIVATE $<BUILD_INTERFACE:Ldk>)
# On ARM64, WDK 10.0.26100's libcntpr archive stores wcslen and wcsnlen in
# the same member. Ldk introduces the wcslen reference after the driver's
# first ntoskrnl import library, so resolve the kernel import immediately
# after Ldk instead of pulling that conflicting libcntpr member.
if(CRTSYS_NATIVE_ARCH STREQUAL "ARM64")
  target_link_libraries(crtsys PRIVATE WDK::NTOSKRNL)
  # Force the ARM64 ctype compatibility object into the final image before
  # libcntpr is scanned. This prevents libcntpr's combined __ctype.obj from
  # colliding with the ctype exports in the kernel import library.
  target_link_options(crtsys PUBLIC "/INCLUDE:iscntrl")
endif()
target_link_libraries(crtsys PRIVATE WDK::AUX_KLIB)
if(CRTSYS_USE_LIBCNTPR)
  target_link_libraries(crtsys PRIVATE WDK::LIBCNTPR)
  # src/ucrt*.props uses this global property to keep UCRT internal state
  # while giving the two libcntpr-owned public entry points private names.
  set_target_properties(
    crtsys PROPERTIES VS_GLOBAL_CrtSysUseLibcntpr "true")
endif()

#
# Add compile definitions
#
target_compile_definitions(crtsys PUBLIC "_KERNEL32_")
target_compile_definitions(crtsys PUBLIC "_ITERATOR_DEBUG_LEVEL=0")
if (CRTSYS_USE_LIBCNTPR)
  target_compile_definitions(crtsys PUBLIC "CRTSYS_USE_LIBCNTPR")
  if ("${CMAKE_VS_WINDOWS_TARGET_PLATFORM_VERSION}" VERSION_GREATER_EQUAL "10.0.22000.0")
    target_compile_definitions(crtsys PRIVATE "CRTSYS_NEED_CRT_IOB_FUNC")
    target_compile_definitions(crtsys PRIVATE "CRTSYS_NEED_CRT_ABORT")
  endif()
else()
  target_compile_definitions(crtsys PRIVATE "CRTSYS_NEED_CRT_IOB_FUNC")
  target_compile_definitions(crtsys PRIVATE "CRTSYS_NEED_CRT_ABORT")
endif()
if (NOT CRTSYS_ENABLE_DIAGNOSTIC_BREAKPOINTS)
  target_compile_definitions(crtsys PRIVATE "CRTSYS_ENABLE_DIAGNOSTIC_BREAKPOINTS=0")
endif()
target_compile_definitions(crtsys PUBLIC "_HAS_EXCEPTIONS")
if (UCXXRT_ENABLED)
  target_compile_definitions(crtsys PUBLIC "UCXXRT")
  target_compile_definitions(crtsys PRIVATE "__KERNEL_MODE")
endif()

#
# Forced Include File
#
set(CRTSYS_WINSDK_FORCED_INCLUDE "${CMAKE_CURRENT_SOURCE_DIR}/include/.internal/winsdk/${CMAKE_VS_WINDOWS_TARGET_PLATFORM_VERSION}/wdk/${WDK_VERSION}/forced.h")
if(NOT EXISTS "${CRTSYS_WINSDK_FORCED_INCLUDE}")
  set(CRTSYS_WINSDK_FORCED_INCLUDE "${CMAKE_CURRENT_SOURCE_DIR}/include/.internal/winsdk/wdk/${WDK_VERSION}/forced.h")
endif()
if(EXISTS "${CRTSYS_WINSDK_FORCED_INCLUDE}")
  target_compile_options(crtsys PRIVATE "$<$<COMPILE_LANGUAGE:C,CXX>:/FI${CRTSYS_WINSDK_FORCED_INCLUDE}>")
endif()
target_compile_options(crtsys PRIVATE "$<$<COMPILE_LANGUAGE:C,CXX>:/FI${CMAKE_CURRENT_SOURCE_DIR}/src/crtsys.h>")
if (UCXXRT_ENABLED)
  target_compile_options(crtsys PRIVATE "$<$<COMPILE_LANGUAGE:C,CXX>:/FI${ucxxrt_SOURCE_DIR}/ucxxrt.inl>")
endif()

crtsys_set_kernel_static_msvc_runtime(crtsys)
target_compile_options(crtsys PRIVATE "$<$<COMPILE_LANGUAGE:C,CXX>:/FS>")

#
# Generate Debug Info
#
target_compile_options(crtsys PUBLIC "$<$<AND:$<CONFIG:Release>,$<COMPILE_LANGUAGE:C,CXX>>:/Zi>")
target_link_options(crtsys PUBLIC "$<$<CONFIG:Release>:/DEBUG>")

#
# Force File Output
#
if (CRTSYS_USE_LIBCNTPR AND
    NOT CRTSYS_NATIVE_ARCH STREQUAL "ARM64" AND
    "${CMAKE_VS_WINDOWS_TARGET_PLATFORM_VERSION}" VERSION_LESS
        "10.0.22000.0")
  # 10.0.17763.0
  # 10.0.22000.0
  # km/x64/libcntpr.lib(fpexcept.obj) : error LNK2005: RaiseException already defined in Ldk.lib(errhandlingapi.obj)

  # 10.0.17763.0
  # km/x64/ntoskrnl.lib(ntoskrnl.exe) : error LNK2005: _wcslen already defined in km/x64/libcntpr.lib(wcslen.obj)

  # 10.0.17763.0
  # libcntpr.lib fflush
  # src\custom\ucrt\common\stdio\fflush.cpp fflush
  # Older libcntpr archives still collide with kernel/UCRT members that
  # cannot be isolated by the modern compatibility metadata below.
  target_link_options(crtsys PUBLIC "/FORCE:MULTIPLE")
elseif(UCXXRT_ENABLED AND CRTSYS_NATIVE_ARCH STREQUAL "x64")
  # ntoskrnl.lib(ntoskrnl.exe) : error LNK2005: RtlRaiseException already defined in Ldk.lib(except.obj).
  target_link_options(crtsys PUBLIC "/FORCE:MULTIPLE")
endif()

if(CRTSYS_BUILD_NTL_KERNEL_CONTENT_CODECS)
  # The optional libraries are separate archives so ordinary crtsys users do
  # not pay for networking codecs.  NuGet packaging enables this option and
  # places the archives in its driver-specific codec layout.
  include(cmake/CrtSys.cmake)
  include(cmake/NtlContentCodecs.cmake)
  crtsys_add_ntl_kernel_content_codecs()
endif()

if(CRTSYS_NATIVE_ARCH STREQUAL "x86")
  # crtsys.lib(lowhelpr.obj) : error LNK2026: module unsafe for SAFESEH image.
  target_link_options(crtsys PUBLIC "/SAFESEH:NO")

  # Remove Runtime Checks
  string(REGEX REPLACE "/RTC(su|[1su])" "" CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS}")
  string(REGEX REPLACE "/RTC(su|[1su])" "" CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG}")
  string(REGEX REPLACE "/RTC(su|[1su])" "" CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE}")
  string(REGEX REPLACE "/RTC(su|[1su])" "" CMAKE_CXX_FLAGS_MINSIZEREL "${CMAKE_CXX_FLAGS_MINSIZEREL}")
  string(REGEX REPLACE "/RTC(su|[1su])" "" CMAKE_CXX_FLAGS_RELWITHDEBINFO "${CMAKE_CXX_FLAGS_RELWITHDEBINFO}")
endif()

#
# set visual studio properties file
#
if (UCXXRT_ENABLED)
  list(APPEND VS_PROPS "${CMAKE_CURRENT_SOURCE_DIR}/src/ucrt_for_ucxxrt.props")
else()
  list(APPEND VS_PROPS "${CMAKE_CURRENT_SOURCE_DIR}/src/vcruntime.props;${CMAKE_CURRENT_SOURCE_DIR}/src/custom/msvc/crt.props;${CMAKE_CURRENT_SOURCE_DIR}/src/ucrt.props")
endif()
set_target_properties(crtsys PROPERTIES VS_USER_PROPS "${CMAKE_CURRENT_SOURCE_DIR}/src/stl.props;${VS_PROPS}")

#
# Set the distributable library output path for a top-level build only.
# Dependency builds must keep their archive inside their own binary tree:
# separate test/consumer configurations can compile crtsys with different
# private definitions, and writing all of them to one source-tree lib path
# lets an unrelated build silently replace another configuration's archive.
#
if(CRTSYS_IS_TOP_LEVEL)
  string(TOLOWER "${CRTSYS_NATIVE_ARCH}" PLATFROM_SHORT_NAME)
  set_target_properties(
    crtsys
    PROPERTIES
      ARCHIVE_OUTPUT_DIRECTORY_RELEASE
        ${CMAKE_CURRENT_SOURCE_DIR}/lib/${PLATFROM_SHORT_NAME}
  )
endif()

set(CRTSYS_NATIVE_TOOLSET "v${MSVC_TOOLSET_VERSION}")

if(CRTSYS_ENABLE_INSTALL)
  include(GNUInstallDirs)
  include(CMakePackageConfigHelpers)

  set(CRTSYS_INSTALL_CMAKEDIR "${CMAKE_INSTALL_DATADIR}/crtsys/cmake" CACHE STRING "crtsys CMake package install directory")
  set(CRTSYS_INSTALL_RELEASE_LIBDIR
      "lib/native/${CRTSYS_NATIVE_TOOLSET}/${CRTSYS_NATIVE_ARCH}/Release"
      CACHE STRING "crtsys Release library install directory")
  set(CRTSYS_INSTALL_DEBUG_LIBDIR
      "lib/native/${CRTSYS_NATIVE_TOOLSET}/${CRTSYS_NATIVE_ARCH}/Debug"
      CACHE STRING "crtsys Debug library install directory")
  set(CRTSYS_INSTALL_LIBDIR
      "$<IF:$<CONFIG:Debug>,${CRTSYS_INSTALL_DEBUG_LIBDIR},${CRTSYS_INSTALL_RELEASE_LIBDIR}>")

  set(CRTSYS_INSTALL_TARGETS crtsys)
  if(TARGET Ldk)
    list(APPEND CRTSYS_INSTALL_TARGETS Ldk)
  endif()

  install(
    TARGETS ${CRTSYS_INSTALL_TARGETS}
    ARCHIVE DESTINATION "${CRTSYS_INSTALL_LIBDIR}"
    LIBRARY DESTINATION "${CRTSYS_INSTALL_LIBDIR}"
    RUNTIME DESTINATION "${CRTSYS_INSTALL_LIBDIR}"
  )

  install(DIRECTORY include/ DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}")
  install(FILES README.md LICENSE DESTINATION "${CMAKE_INSTALL_DOCDIR}")
  install(DIRECTORY docs/ DESTINATION "${CMAKE_INSTALL_DOCDIR}/docs")
  install(
    FILES
      cmake/CPM.cmake
      cmake/CrtSys.cmake
      cmake/FindWDK.cmake
      cmake/NtlContentCodecs.cmake
      cmake/NtlMsQuic.cmake
      cmake/pkg-utils.cmake
      cmake/std-cxx.cmake
    DESTINATION "${CRTSYS_INSTALL_CMAKEDIR}"
  )
  install(
    DIRECTORY cmake/vendor/findwdk/
    DESTINATION "${CRTSYS_INSTALL_CMAKEDIR}/vendor/findwdk"
  )

  configure_package_config_file(
    cmake/crtsys-config.cmake.in
    "${CMAKE_CURRENT_BINARY_DIR}/crtsys-config.cmake"
    INSTALL_DESTINATION "${CRTSYS_INSTALL_CMAKEDIR}"
  )
  write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/crtsys-config-version.cmake"
    VERSION "${PROJECT_VERSION}"
    COMPATIBILITY SameMajorVersion
  )
  install(
    FILES
      "${CMAKE_CURRENT_BINARY_DIR}/crtsys-config.cmake"
      "${CMAKE_CURRENT_BINARY_DIR}/crtsys-config-version.cmake"
    DESTINATION "${CRTSYS_INSTALL_CMAKEDIR}"
  )

endif()
