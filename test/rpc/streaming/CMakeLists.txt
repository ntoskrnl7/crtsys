cmake_minimum_required(VERSION 3.14 FATAL_ERROR)

project(crtsys_rpc_streaming LANGUAGES C CXX)

include("${CMAKE_CURRENT_LIST_DIR}/../../../cmake/CPM.cmake")

set(CRTSYS_NTL_MAIN ON)
CPMAddPackage(NAME crtsys SOURCE_DIR "${CMAKE_CURRENT_LIST_DIR}/../../..")
include("${crtsys_SOURCE_DIR}/cmake/CrtSys.cmake")

crtsys_add_driver(crtsys_rpc_streaming_driver
  driver/main.cpp
)

add_executable(crtsys_rpc_streaming_app
  app/main.cpp
)

target_include_directories(crtsys_rpc_streaming_driver
  PRIVATE "${CMAKE_CURRENT_LIST_DIR}/shared"
)

target_include_directories(crtsys_rpc_streaming_app
  PRIVATE
    "${crtsys_SOURCE_DIR}/include"
    "${CMAKE_CURRENT_LIST_DIR}/shared"
)

target_compile_options(crtsys_rpc_streaming_driver
  PRIVATE
    "$<$<COMPILE_LANGUAGE:CXX>:/W4>"
    "$<$<COMPILE_LANGUAGE:CXX>:/WX>"
)

target_compile_options(crtsys_rpc_streaming_app
  PRIVATE
    "$<$<COMPILE_LANGUAGE:CXX>:/W4>"
    "$<$<COMPILE_LANGUAGE:CXX>:/WX>"
    "$<$<COMPILE_LANGUAGE:CXX>:/EHsc>"
    "$<$<AND:$<COMPILE_LANGUAGE:CXX>,$<CONFIG:Debug>>:/MTd>"
    "$<$<AND:$<COMPILE_LANGUAGE:CXX>,$<CONFIG:Release>>:/MT>"
)

include("${crtsys_SOURCE_DIR}/cmake/std-cxx.cmake")
set_std_cxx_latest(crtsys_rpc_streaming_driver)
set_std_cxx_latest(crtsys_rpc_streaming_app)
