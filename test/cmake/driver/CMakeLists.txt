cmake_minimum_required(VERSION 3.14 FATAL_ERROR)

set(CMAKE_POLICY_VERSION_MINIMUM 3.5 CACHE STRING "Allow older third-party CMake projects under CMake 4.")

project(crtsys_test LANGUAGES C CXX)

cmake_policy(SET CMP0079 NEW)

include(../../../cmake/CPM.cmake)

option(CRTSYS_TEST_BREAKPOINT "Break into the kernel debugger when the test driver loads" OFF)
option(CRTSYS_ENABLE_FAILURE_HARNESS_SELF_TEST "Enable an intentional driver-test failure to verify exception capture" OFF)

set(CRTSYS_NTL_MAIN ON)
CPMAddPackage(NAME crtsys SOURCE_DIR ${CMAKE_CURRENT_LIST_DIR}/../../..)
include(${crtsys_SOURCE_DIR}/cmake/CrtSys.cmake)

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
target_compile_options(crtsys_test PRIVATE "$<$<COMPILE_LANGUAGE:CXX>:/GR>")
# Keep cppreference examples such as std::fopen intact while building the
# driver test harness with /WX under older MSVC toolsets.
target_compile_definitions(crtsys_test PRIVATE "_CRT_SECURE_NO_WARNINGS")

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

target_link_libraries(crtsys_test gtest)

target_include_directories(crtsys_test PRIVATE ${CMAKE_CURRENT_LIST_DIR}/..)

include(../../../cmake/std-cxx.cmake)
set_std_cxx_latest(crtsys_test)
