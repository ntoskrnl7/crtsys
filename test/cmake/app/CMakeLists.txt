cmake_minimum_required(VERSION 3.14 FATAL_ERROR)

set(CMAKE_POLICY_VERSION_MINIMUM 3.5 CACHE STRING "Allow older third-party CMake projects under CMake 4.")

project(crtsys_test_app LANGUAGES C CXX)

include(../../../cmake/CPM.cmake)
CPMAddPackage(
    NAME googletest
    GITHUB_REPOSITORY google/googletest
    GIT_TAG release-1.10.0
    VERSION 1.10.0
    OPTIONS "INSTALL_GTEST OFF" "BUILD_GMOCK OFF" "gtest_force_shared_crt"
  )

add_executable(crtsys_test_app
    src/main.cpp
    src/rpc_coroutine.cpp)

set_source_files_properties(src/rpc_coroutine.cpp PROPERTIES
    COMPILE_OPTIONS "/std:c++20;/Zc:__cplusplus")

target_compile_options(crtsys_test_app PRIVATE "/EHsc")

target_link_libraries(crtsys_test_app gtest)

target_include_directories(crtsys_test_app PRIVATE ${CMAKE_CURRENT_LIST_DIR}/../../../include ${CMAKE_CURRENT_LIST_DIR}/..)
