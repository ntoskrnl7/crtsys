cmake_minimum_required(VERSION 3.14 FATAL_ERROR)

project(crtsys_test LANGUAGES C)

include(../cmake/CPM.cmake)

# set(CRTSYS_USE_LIBCNTPR ON)
set(CRTSYS_NTL_MAIN OFF)
CPMAddPackage(NAME crtsys SOURCE_DIR ${CMAKE_CURRENT_LIST_DIR}/..)
# CPMAddPackage("gh:ntoskrnl7/crtsys@0.1.0")
include(${crtsys_SOURCE_DIR}/cmake/CrtSys.cmake)

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

include(../cmake/std-cxx.cmake)
set_std_cxx_latest(crtsys_test)