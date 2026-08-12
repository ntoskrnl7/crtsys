cmake_minimum_required(VERSION 3.14 FATAL_ERROR)

set(CMAKE_POLICY_VERSION_MINIMUM 3.5 CACHE STRING "Allow older third-party CMake projects under CMake 4.")

project(crtsys_test LANGUAGES C CXX)

cmake_policy(SET CMP0079 NEW)

include(../../../cmake/CPM.cmake)

option(CRTSYS_TEST_BREAKPOINT "Break into the kernel debugger when the test driver loads" OFF)
option(CRTSYS_ENABLE_FAILURE_HARNESS_SELF_TEST "Enable an intentional driver-test failure to verify exception capture" OFF)
option(
    CRTSYS_TEST_EXTENDED_PMR_BENCHMARK
    "Run the long cppreference PMR allocation benchmark in the kernel test driver"
    OFF
)

set(CRTSYS_NTL_MAIN ON)
CPMAddPackage(NAME crtsys SOURCE_DIR ${CMAKE_CURRENT_LIST_DIR}/../../..)
include(${crtsys_SOURCE_DIR}/cmake/CrtSys.cmake)
target_compile_definitions(crtsys PRIVATE "CRTSYS_ENABLE_STACKTRACE_TEST_HOOKS")
target_compile_definitions(crtsys PRIVATE "CRTSYS_ENABLE_RUNTIME_DIAGNOSTIC_TEST_HOOKS")

set(CRTSYS_TEST_NLOHMANN_JSON ON)

if (CRTSYS_TEST_NLOHMANN_JSON)
    CPMAddPackage(
        NAME nlohmann_json
        VERSION 3.10.5
        GITHUB_REPOSITORY nlohmann/json
        OPTIONS 
            "JSON_BuildTests OFF"
    )
endif()

CPMAddPackage(
    NAME googletest
    GITHUB_REPOSITORY google/googletest
    GIT_TAG release-1.11.0
    VERSION 1.11.0
    OPTIONS
        "INSTALL_GTEST OFF"
        "BUILD_GMOCK OFF"
)
target_link_libraries(gtest crtsys)
target_compile_options(gtest PRIVATE "/Gz")
target_link_libraries(gtest_main PRIVATE crtsys)
target_compile_options(gtest_main PRIVATE "/Gd")
if(CMAKE_VS_PLATFORM_NAME STREQUAL "ARM64" AND MSVC_VERSION LESS 1930)
    # WDK 10.0.26100's winnt.h uses the intrinsic before the v142 ARM64
    # headers declare it. Keep the test framework compilable so the matrix
    # reaches and validates the final crtsys_test.sys link.
    target_compile_options(
        gtest
        PRIVATE
            "/FI${CMAKE_CURRENT_SOURCE_DIR}/v142_arm64_intrinsics.h"
    )
endif()

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /W4 /WX")
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} /W4 /WX")

file(GLOB SOURCE_FILES
    src/*.cpp
    src/*/*.cpp
    src/*/*/*.cpp
    src/*.c
    src/*/*.c
    src/*/*/*.c
    )

  list(APPEND SOURCE_FILES
    ${CMAKE_CURRENT_SOURCE_DIR}/../../flt/compile/registration_and_ownership.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../../flt/compile/operation_callback.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../../flt/compile/context.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../../flt/compile/control_device.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../../flt/compile/instance.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../../flt/compile/rpc_header_self_contained.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../../flt/compile/volume_metadata.cpp
  )

if (NOT CRTSYS_TEST_NLOHMANN_JSON)
    list(REMOVE_ITEM SOURCE_FILES ${CMAKE_CURRENT_SOURCE_DIR}/src/libs/nlohmann_json.cpp)
endif()

if(MSVC_VERSION GREATER_EQUAL 1950)
    set_source_files_properties(
        ${CMAKE_CURRENT_SOURCE_DIR}/src/libs/nlohmann_json.cpp
        PROPERTIES COMPILE_OPTIONS "/wd5311"
    )
endif()

crtsys_add_driver(crtsys_test ${SOURCE_FILES})
target_link_libraries(crtsys_test WDK::FLTMGR)
target_compile_options(crtsys_test PRIVATE "$<$<COMPILE_LANGUAGE:CXX>:/GR>")
# Keep cppreference examples such as std::fopen intact while building the
# driver test harness with /WX under older MSVC toolsets.
target_compile_definitions(crtsys_test PRIVATE "_CRT_SECURE_NO_WARNINGS")
target_compile_definitions(crtsys_test PRIVATE "CRTSYS_ENABLE_STACKTRACE_TEST_HOOKS")
target_compile_definitions(crtsys_test PRIVATE "CRTSYS_ENABLE_RUNTIME_DIAGNOSTIC_TEST_HOOKS")

if (NOT CRTSYS_TEST_BREAKPOINT)
    target_compile_definitions(crtsys_test PRIVATE "CRTSYS_TEST_NO_BREAKPOINT")
endif()

if (CRTSYS_TEST_NLOHMANN_JSON)
    target_compile_definitions(crtsys_test PRIVATE "CRTSYS_TEST_NLOHMANN_JSON")
    target_link_libraries(crtsys_test nlohmann_json::nlohmann_json)
endif()

if (CRTSYS_ENABLE_FAILURE_HARNESS_SELF_TEST)
    target_compile_definitions(crtsys_test PRIVATE "CRTSYS_ENABLE_FAILURE_HARNESS_SELF_TEST=1")
endif()

if (CRTSYS_TEST_EXTENDED_PMR_BENCHMARK)
    target_compile_definitions(crtsys_test PRIVATE "CRTSYS_TEST_EXTENDED_PMR_BENCHMARK=1")
endif()

target_link_libraries(crtsys_test gtest)

# crtsys.lib is built against the Windows 8 Filter Manager surface while the
# supported consumer range starts at Windows 7. The provider's template symbol
# encodes both public owner sizes; the Win8 consumer can link only when the
# layouts are identical across that prebuilt-library boundary.
wdk_add_library(
    crtsys_flt_abi_win7
    EXTENDED_CPP_FEATURES
    WINVER 0x0601
    STATIC
    ${CMAKE_CURRENT_SOURCE_DIR}/../../flt/compile/abi_win7_provider.cpp
)
wdk_add_library(
    crtsys_flt_abi_win8
    EXTENDED_CPP_FEATURES
    WINVER 0x0602
    STATIC
    ${CMAKE_CURRENT_SOURCE_DIR}/../../flt/compile/abi_win8_consumer.cpp
)
target_link_libraries(crtsys_flt_abi_win7 PRIVATE crtsys)
target_link_libraries(crtsys_flt_abi_win8 PRIVATE crtsys)
crtsys_apply_driver_settings(
    crtsys_flt_abi_win7
    "${crtsys_SOURCE_DIR}"
    FALSE
    FALSE
    FALSE
)
crtsys_apply_driver_settings(
    crtsys_flt_abi_win8
    "${crtsys_SOURCE_DIR}"
    FALSE
    FALSE
    FALSE
)
target_compile_definitions(
    crtsys_flt_abi_win7
    PRIVATE
        _KERNEL32_
        _ITERATOR_DEBUG_LEVEL=0
        _HAS_EXCEPTIONS
)
target_compile_definitions(
    crtsys_flt_abi_win8
    PRIVATE
        _KERNEL32_
        _ITERATOR_DEBUG_LEVEL=0
        _HAS_EXCEPTIONS
)
target_compile_options(
    crtsys_flt_abi_win7
    PRIVATE
        "$<$<COMPILE_LANGUAGE:C,CXX>:/MT>"
)
target_compile_options(
    crtsys_flt_abi_win8
    PRIVATE
        "$<$<COMPILE_LANGUAGE:C,CXX>:/MT>"
)
target_link_libraries(
    crtsys_test
    crtsys_flt_abi_win7
    crtsys_flt_abi_win8
)

target_include_directories(crtsys_test PRIVATE ${CMAKE_CURRENT_LIST_DIR}/..)

include(../../../cmake/std-cxx.cmake)
set_std_cxx_latest(crtsys_test)
set_std_cxx_latest(crtsys_flt_abi_win7)
set_std_cxx_latest(crtsys_flt_abi_win8)

crtsys_add_driver(crtsys_multi_a multi_instance/driver.cpp)
target_compile_definitions(crtsys_multi_a PRIVATE "CRTSYS_MULTI_INSTANCE_ID=1")
target_include_directories(crtsys_multi_a PRIVATE ${CMAKE_CURRENT_LIST_DIR}/..)
set_std_cxx_latest(crtsys_multi_a)

crtsys_add_driver(crtsys_multi_b multi_instance/driver.cpp)
target_compile_definitions(crtsys_multi_b PRIVATE "CRTSYS_MULTI_INSTANCE_ID=2")
target_include_directories(crtsys_multi_b PRIVATE ${CMAKE_CURRENT_LIST_DIR}/..)
set_std_cxx_latest(crtsys_multi_b)

add_executable(crtsys_multi_probe multi_instance/probe.cpp)
target_include_directories(crtsys_multi_probe PRIVATE ${CMAKE_CURRENT_LIST_DIR}/..)
target_compile_options(
    crtsys_multi_probe
    PRIVATE
        "$<$<CONFIG:Debug>:/MTd>"
        "$<$<NOT:$<CONFIG:Debug>>:/MT>"
)
set_std_cxx_latest(crtsys_multi_probe)
