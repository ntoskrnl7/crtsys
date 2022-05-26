cmake_minimum_required(VERSION 3.14 FATAL_ERROR)

project(crtsys_test LANGUAGES C CXX)

include(../cmake/CPM.cmake)

set(CRTSYS_USE_LIBCNTPR ON)
set(CRTSYS_NTL_MAIN ON)
CPMAddPackage(NAME crtsys SOURCE_DIR ${CMAKE_CURRENT_LIST_DIR}/..)
# CPMAddPackage("gh:ntoskrnl7/crtsys@0.1.0")
include(${crtsys_SOURCE_DIR}/cmake/CrtSys.cmake)

CPMAddPackage("gh:nlohmann/json@3.10.5")

CPMAddPackage(
    NAME googletest
    GITHUB_REPOSITORY google/googletest
    GIT_TAG release-1.11.0
    VERSION 1.11.0
    OPTIONS "INSTALL_GTEST OFF" "BUILD_GMOCK OFF"
  )
target_link_libraries(gtest crtsys)
target_compile_options(gtest PRIVATE "/Gz")
target_link_libraries(gtest_main PRIVATE crtsys)
target_compile_options(gtest_main PRIVATE "/Gd")

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /W4 /WX")
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} /W4 /WX")

file(GLOB SOURCE_FILES
    ./src/*.cpp
    ./src/*/*.cpp
    ./src/*/*/*.cpp
    ./src/*.c
    ./src/*/*.c
    ./src/*/*/*.c
    )

crtsys_add_driver(crtsys_test ${SOURCE_FILES})

target_link_libraries(crtsys_test gtest nlohmann_json::nlohmann_json)

include(../cmake/std-cxx.cmake)
set_std_cxx_latest(crtsys_test)