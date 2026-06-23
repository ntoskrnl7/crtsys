cmake_minimum_required(VERSION 3.14)

# INCLUDE_DIRECTORIES $(VC_IncludePath);$(WindowsSDK_IncludePath)
cmake_policy(SET CMP0021 OLD)

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
option(CRTSYS_ENABLE_DIAGNOSTIC_BREAKPOINTS "Set to OFF to suppress crtsys diagnostic KdBreakPoint calls" ON)
option(CRTSYS_ENABLE_INSTALL "Enable crtsys install rules" ${CRTSYS_IS_TOP_LEVEL})

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

# x64 is always set to use libcntpr. (_NLG_Notify, _NLG_Return2)
if ("${CMAKE_VS_PLATFORM_NAME}" STREQUAL "x64")
  set(CRTSYS_USE_LIBCNTPR ON)
endif()

if(MSVC_TOOLSET_VERSION EQUAL 142 OR MSVC_TOOLSET_VERSION EQUAL 143)
  set(UCXXRT_ENABLED OFF)
else()
  set(UCXXRT_ENABLED ON)
endif()

include(cmake/CPM.cmake)

if ("${CMAKE_VS_PLATFORM_NAME}" STREQUAL "x64")
  set(LDK_USE_RAISE_EXCEPTION ON)
  set(LDK_USE_RTL_RAISE_EXCEPTION ON)
else()
  set(LDK_USE_RAISE_EXCEPTION OFF)
  set(LDK_USE_RTL_RAISE_EXCEPTION OFF)
endif()
if (NOT CRTSYS_ENABLE_DIAGNOSTIC_BREAKPOINTS)
  set(LDK_ENABLE_DIAGNOSTIC_BREAKPOINTS OFF CACHE BOOL "Set to OFF to suppress LDK diagnostic KdBreakPoint calls")
endif()
CPMAddPackage("gh:ntoskrnl7/Ldk@0.7.7")

CPMAddPackage("gh:ntoskrnl7/FindWDK#master")
list(APPEND CMAKE_MODULE_PATH "${FindWDK_SOURCE_DIR}/cmake")
find_package(WDK REQUIRED)

#
# Add source files.
#

file(GLOB SOURCE_FILES
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

if("${CMAKE_VS_PLATFORM_NAME}" STREQUAL "Win32")
  file(GLOB MATH_SOURCE_FILES
    src/custom/crt/math/*.c
    )
  list(APPEND SOURCE_FILES ${MATH_SOURCE_FILES})
  list(APPEND SOURCE_FILES src/custom/crt/tls_x86.asm)
else()
  file(GLOB MATH_SOURCE_FILES
  src/custom/crt/math/nextafter.c
  src/custom/crt/math/fpclassify.c
  src/custom/crt/math/fma.c
  src/custom/crt/math/real_helpers.c
  )
  list(APPEND SOURCE_FILES ${MATH_SOURCE_FILES})
endif()

if (UCXXRT_ENABLED)
  list(APPEND SOURCE_FILES src/custom/msvc/common/crt/src/stl/winapisupp.cpp)
  list(REMOVE_ITEM SOURCE_FILES ${CMAKE_CURRENT_SOURCE_DIR}/src/custom/crt/math/ftol3.c)

  CPMAddPackage("gh:ntoskrnl7/ucxxrt#master")
 
  file(GLOB_RECURSE UCXXRT_SOURCE_FILES
    ${ucxxrt_SOURCE_DIR}/src/*.cpp
    ${ucxxrt_SOURCE_DIR}/vcruntime/*.cpp
    ${ucxxrt_SOURCE_DIR}/misc/*.cpp
    )
  list(REMOVE_ITEM UCXXRT_SOURCE_FILES ${ucxxrt_SOURCE_DIR}/src/vcruntime/crt.cpp)
  list(REMOVE_ITEM UCXXRT_SOURCE_FILES ${ucxxrt_SOURCE_DIR}/src/vcruntime/locales.cpp)
  list(REMOVE_ITEM UCXXRT_SOURCE_FILES ${ucxxrt_SOURCE_DIR}/src/vcruntime/thread.cpp)  
  if(NOT "${CMAKE_VS_PLATFORM_NAME}" STREQUAL "Win32" AND NOT "${CMAKE_VS_PLATFORM_NAME}" STREQUAL "x64" )
    list(REMOVE_ITEM UCXXRT_SOURCE_FILES ${ucxxrt_SOURCE_DIR}/src/vcruntime/x86_exception_filter.cpp)
  endif()
  list(REMOVE_ITEM UCXXRT_SOURCE_FILES ${ucxxrt_SOURCE_DIR}/src/cpu_disp.cpp)
  list(REMOVE_ITEM UCXXRT_SOURCE_FILES ${ucxxrt_SOURCE_DIR}/src/terminate.cpp)
  list(REMOVE_ITEM UCXXRT_SOURCE_FILES ${ucxxrt_SOURCE_DIR}/src/misc/invalid_parameter.cpp)
  list(REMOVE_ITEM UCXXRT_SOURCE_FILES ${ucxxrt_SOURCE_DIR}/src/sys_main.cpp)
  list(REMOVE_ITEM UCXXRT_SOURCE_FILES ${ucxxrt_SOURCE_DIR}/src/unittest.cpp)
  if("${CMAKE_VS_PLATFORM_NAME}" STREQUAL "x64")
    file(GLOB PLATFORM_SOURCE_FILES
      ${ucxxrt_SOURCE_DIR}/src/x64/*.cpp
      ${ucxxrt_SOURCE_DIR}/src/x64/*.asm
    )
  elseif("${CMAKE_VS_PLATFORM_NAME}" STREQUAL "Win32")
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
    STATIC
    ${SOURCE_FILES}
    )
crtsys_scope_compile_options_to_c_cxx(crtsys)

#
# Add include directories
#
get_target_property(INC_DIR_TMP crtsys INCLUDE_DIRECTORIES)
if (UCXXRT_ENABLED)
  set_property(TARGET crtsys PROPERTY INCLUDE_DIRECTORIES "${CMAKE_CURRENT_SOURCE_DIR}/src/custom/winsdk/include/um/crtsys;${Ldk_SOURCE_DIR}/include/Ldk;${ucxxrt_SOURCE_DIR}/src;${ucxxrt_SOURCE_DIR}/src/vcruntime;$(VC_IncludePath);$(WindowsSDK_IncludePath);${INC_DIR_TMP}")
  target_include_directories(crtsys PRIVATE "${ucxxrt_SOURCE_DIR}")
else()
  set_property(TARGET crtsys PROPERTY INCLUDE_DIRECTORIES "${CMAKE_CURRENT_SOURCE_DIR}/src/custom/winsdk/include/um/crtsys;${Ldk_SOURCE_DIR}/include/Ldk;$(VC_IncludePath);$(WindowsSDK_IncludePath);${INC_DIR_TMP}")
endif()

target_include_directories(crtsys PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src/custom/msvc/${MSVC_TOOLSET_VERSION}/include")
target_include_directories(crtsys PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src/custom/msvc/${MSVC_TOOLSET_VERSION}/crt/src/vcruntime")
target_include_directories(crtsys PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src/custom/msvc/${MSVC_TOOLSET_VERSION}/crt/src/stl")
target_include_directories(crtsys BEFORE PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src/custom/ucrt/${CMAKE_VS_WINDOWS_TARGET_PLATFORM_VERSION}/inc")

#
# link libraries
#
target_link_libraries(crtsys PRIVATE $<BUILD_INTERFACE:Ldk>)
if(CRTSYS_USE_LIBCNTPR)
  target_link_libraries(crtsys PRIVATE WDK::LIBCNTPR)
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
if (CRTSYS_NTL_MAIN)
  target_compile_definitions(crtsys PUBLIC CRTSYS_USE_NTL_MAIN)
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
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/include/.internal/winsdk/${CMAKE_VS_WINDOWS_TARGET_PLATFORM_VERSION}/wdk/${WDK_VERSION}/forced.h")
  target_compile_options(crtsys PRIVATE "$<$<COMPILE_LANGUAGE:C,CXX>:/FI${CMAKE_CURRENT_SOURCE_DIR}/include/.internal/winsdk/${CMAKE_VS_WINDOWS_TARGET_PLATFORM_VERSION}/wdk/${WDK_VERSION}/forced.h>")
endif()
target_compile_options(crtsys PRIVATE "$<$<COMPILE_LANGUAGE:C,CXX>:/FI${CMAKE_CURRENT_SOURCE_DIR}/src/crtsys.h>")
if (UCXXRT_ENABLED)
  target_compile_options(crtsys PRIVATE "$<$<COMPILE_LANGUAGE:C,CXX>:/FI${ucxxrt_SOURCE_DIR}/ucxxrt.inl>")
endif()

target_compile_options(crtsys PUBLIC "$<$<COMPILE_LANGUAGE:C,CXX>:/MT>") # "/MT$<$<CONFIG:Debug>:d>"
# [not working] set_property(TARGET crtsys PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

#
# Generate Debug Info
#
target_compile_options(crtsys PUBLIC "$<$<AND:$<CONFIG:Release>,$<COMPILE_LANGUAGE:C,CXX>>:/Zi>")
target_link_options(crtsys PUBLIC "$<$<CONFIG:Release>:/DEBUG>")

#
# Force File Output
#
if (CRTSYS_USE_LIBCNTPR)
  # 10.0.17763.0
  # 10.0.22000.0
  # km/x64/libcntpr.lib(fpexcept.obj) : error LNK2005: RaiseException already defined in Ldk.lib(errhandlingapi.obj)

  # 10.0.17763.0
  # km/x64/ntoskrnl.lib(ntoskrnl.exe) : error LNK2005: _wcslen already defined in km/x64/libcntpr.lib(wcslen.obj)

  # 10.0.17763.0
  # libcntpr.lib fflush
  # src\custom\ucrt\common\stdio\fflush.cpp fflush
  target_link_options(crtsys PUBLIC "/FORCE:MULTIPLE")
elseif(UCXXRT_ENABLED AND "${CMAKE_VS_PLATFORM_NAME}" STREQUAL "x64")
  # ntoskrnl.lib(ntoskrnl.exe) : error LNK2005: RtlRaiseException already defined in Ldk.lib(except.obj).
  target_link_options(crtsys PUBLIC "/FORCE:MULTIPLE")
endif()

if("${CMAKE_VS_PLATFORM_NAME}" STREQUAL "Win32")
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
# set library output path (release only)
#
string(TOLOWER "${CMAKE_VS_PLATFORM_NAME}" PLATFROM_SHORT_NAME)
if(PLATFROM_SHORT_NAME STREQUAL "win32")
    set(PLATFROM_SHORT_NAME "x86")
endif()
set_target_properties(crtsys PROPERTIES ARCHIVE_OUTPUT_DIRECTORY_RELEASE ${CMAKE_CURRENT_SOURCE_DIR}/lib/${PLATFROM_SHORT_NAME})

if("${CMAKE_VS_PLATFORM_NAME}" STREQUAL "Win32")
  set(CRTSYS_NATIVE_ARCH x86)
elseif("${CMAKE_VS_PLATFORM_NAME}" STREQUAL "ARM64")
  set(CRTSYS_NATIVE_ARCH ARM64)
elseif("${CMAKE_VS_PLATFORM_NAME}" STREQUAL "ARM")
  set(CRTSYS_NATIVE_ARCH ARM)
else()
  set(CRTSYS_NATIVE_ARCH "${CMAKE_VS_PLATFORM_NAME}")
endif()

if(CRTSYS_ENABLE_INSTALL)
  include(GNUInstallDirs)
  include(CMakePackageConfigHelpers)

  set(CRTSYS_INSTALL_CMAKEDIR "${CMAKE_INSTALL_DATADIR}/crtsys/cmake" CACHE STRING "crtsys CMake package install directory")
  set(CRTSYS_INSTALL_LIBDIR "lib/native/${CRTSYS_NATIVE_ARCH}/$<CONFIG>")

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
      cmake/pkg-utils.cmake
      cmake/std-cxx.cmake
    DESTINATION "${CRTSYS_INSTALL_CMAKEDIR}"
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
