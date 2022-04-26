cmake_minimum_required(VERSION 3.14)

# INCLUDE_DIRECTORIES $(VC_IncludePath);$(WindowsSDK_IncludePath)
cmake_policy(SET CMP0021 OLD)

project(
    crtsys
    VERSION 0.1.0
    DESCRIPTION "C Runtime Lbrary for Windows driver file (*.sys)"
    HOMEPAGE_URL "https://github.com/ntoskrnl7/crtsys"
    LANGUAGES C CXX ASM_MASM
    )

option(CRTSYS_NTL_MAIN      "Set to ON to use ntl::main" ON)
option(CRTSYS_USE_LIBCNTPR  "Set to ON to use libcntpr" ON)

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
CPMAddPackage("gh:ntoskrnl7/Ldk#features/kernel32")

CPMAddPackage("gh:ntoskrnl7/FindWDK#master")
list(APPEND CMAKE_MODULE_PATH "${FindWDK_SOURCE_DIR}/cmake")
find_package(WDK REQUIRED)

if (UCXXRT_ENABLED)
  CPMAddPackage("gh:ntoskrnl7/ucxxrt#master")

  file(GLOB_RECURSE SOURCE_FILES
    ${ucxxrt_SOURCE_DIR}/src/*.cpp
    ${ucxxrt_SOURCE_DIR}/vcruntime/*.cpp
    ${ucxxrt_SOURCE_DIR}/misc/*.cpp
    )

  list(REMOVE_ITEM SOURCE_FILES ${ucxxrt_SOURCE_DIR}/src/sys_main.cpp)
  list(REMOVE_ITEM SOURCE_FILES ${ucxxrt_SOURCE_DIR}/src/unittest.cpp)

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

  list(APPEND SOURCE_FILES ${PLATFORM_SOURCE_FILES})
  add_definitions(/FI"${ucxxrt_SOURCE_DIR}/ucxxrt.inl")

  list(APPEND SOURCE_FILES
    src/main.cpp
    src/custom/common/misc.cpp
    src/custom/common/math/fpconst.c
    src/custom/common/math/pow.c
    src/custom/common/math/pow.c
    src/custom/msvc/common/crt/src/stl/winapisupp.cpp
    )
else()
  file(GLOB SOURCE_FILES
    src/*.cpp

    # common
    src/custom/common/*.cpp
    src/custom/common/*.c
    src/custom/common/*/*.cpp

    # ucrt
    src/custom/ucrt/common/*/*.cpp
    src/custom/ucrt/${CMAKE_VS_WINDOWS_TARGET_PLATFORM_VERSION}/*.cpp
    )

  if("${CMAKE_VS_PLATFORM_NAME}" STREQUAL "Win32")
    file(GLOB MATH_SOURCE_FILES
      src/custom/common/math/*.c
      )
    list(APPEND SOURCE_FILES ${MATH_SOURCE_FILES})
  endif()
endif()

wdk_add_library(
    crtsys
    EXTENDED_CPP_FEATURES
    STATIC
    ${SOURCE_FILES}
    )

if (CRTSYS_USE_LIBCNTPR)
  target_compile_definitions(crtsys PUBLIC "CRTSYS_USE_LIBCNTPR")
  if ("${CMAKE_VS_WINDOWS_TARGET_PLATFORM_VERSION}" VERSION_GREATER_EQUAL "10.0.22000.0")
    target_compile_definitions(crtsys PRIVATE "CRTSYS_NEED_CRT")
  endif()
else()
  target_compile_definitions(crtsys PRIVATE "CRTSYS_NEED_CRT")
endif()

if (CRTSYS_NTL_MAIN)
  target_compile_definitions(crtsys PUBLIC CRTSYS_USE_NTL_MAIN)
endif()
target_compile_definitions(crtsys PUBLIC "_HAS_EXCEPTIONS")



if (UCXXRT_ENABLED)
  target_compile_definitions(crtsys PRIVATE "UCXXRT")
  target_compile_definitions(crtsys PRIVATE "__KERNEL_MODE")
  target_include_directories(crtsys PRIVATE "${ucxxrt_SOURCE_DIR}")
endif()



get_target_property(INC_DIR_TMP crtsys INCLUDE_DIRECTORIES)
if (UCXXRT_ENABLED)
  set_property(TARGET crtsys PROPERTY INCLUDE_DIRECTORIES "${Ldk_SOURCE_DIR}/include/Ldk;${ucxxrt_SOURCE_DIR}/src;${ucxxrt_SOURCE_DIR}/src/vcruntime;${CMAKE_CURRENT_SOURCE_DIR}/src/custom/winsdk/include/um;$(VC_IncludePath);$(WindowsSDK_IncludePath);${INC_DIR_TMP}")
else()
  set_property(TARGET crtsys PROPERTY INCLUDE_DIRECTORIES "${Ldk_SOURCE_DIR}/include/Ldk;${CMAKE_CURRENT_SOURCE_DIR}/src/custom/winsdk/include/um;$(VC_IncludePath);$(WindowsSDK_IncludePath);${INC_DIR_TMP}")
endif()

target_include_directories(crtsys PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src/custom/msvc/${MSVC_TOOLSET_VERSION}/include")
target_include_directories(crtsys PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src/custom/msvc/${MSVC_TOOLSET_VERSION}/crt/src/vcruntime")
target_include_directories(crtsys PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src/custom/msvc/${MSVC_TOOLSET_VERSION}/crt/src/stl")


add_definitions(/FI"${CMAKE_CURRENT_SOURCE_DIR}/src/crtsys.h")
target_compile_options(crtsys PUBLIC /Zc:threadSafeInit-)

if (UCXXRT_ENABLED AND "${CMAKE_VS_PLATFORM_NAME}" STREQUAL "x64")
  # VS2017 Build Tools
  # ntoskrnl.lib(ntoskrnl.exe) : error LNK2005: RtlRaiseException already defined in Ldk.lib(except.obj).
  target_link_options(crtsys PUBLIC "/FORCE:MULTIPLE")
endif()

if (CRTSYS_USE_LIBCNTPR AND "${CMAKE_VS_PLATFORM_NAME}" STREQUAL "x64")
  # 10.0.17763.0
  # 10.0.22000.0
  # km/x64/libcntpr.lib(fpexcept.obj) : error LNK2005: RaiseException already defined in Ldk.lib(errhandlingapi.obj)
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

target_compile_options(crtsys PUBLIC "/MT") # "/MT$<$<CONFIG:Debug>:d>"
# [not working] set_property(TARGET crtsys PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
target_compile_definitions(crtsys PUBLIC "_ITERATOR_DEBUG_LEVEL=0")

if (UCXXRT_ENABLED)
  set_target_properties(crtsys PROPERTIES VS_USER_PROPS "${CMAKE_CURRENT_SOURCE_DIR}/src/stl.props")
else()
  set_target_properties(crtsys PROPERTIES VS_USER_PROPS "${CMAKE_CURRENT_SOURCE_DIR}/src/stl.props;${CMAKE_CURRENT_SOURCE_DIR}/src/vcruntime.props;${CMAKE_CURRENT_SOURCE_DIR}/src/custom/msvc/crt.props")
endif()



if(CRTSYS_USE_LIBCNTPR)
  target_link_libraries(crtsys PRIVATE $<BUILD_INTERFACE:Ldk> WDK::LIBCNTPR)
else()
  target_link_libraries(crtsys PRIVATE $<BUILD_INTERFACE:Ldk>)
endif()



#
# set library output path (release only)
#
string(TOLOWER "${CMAKE_VS_PLATFORM_NAME}" PLATFROM_SHORT_NAME)
if(PLATFROM_SHORT_NAME STREQUAL "win32")
    set(PLATFROM_SHORT_NAME "x86")
endif()
set_target_properties(crtsys PROPERTIES ARCHIVE_OUTPUT_DIRECTORY_RELEASE ${CMAKE_CURRENT_SOURCE_DIR}/lib/${PLATFROM_SHORT_NAME})