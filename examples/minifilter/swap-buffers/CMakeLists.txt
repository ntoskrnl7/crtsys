cmake_minimum_required(VERSION 3.14 FATAL_ERROR)

project(crtsys_minifilter_swap_buffers_sample LANGUAGES C CXX)

include("${CMAKE_CURRENT_LIST_DIR}/../../../cmake/CPM.cmake")

CPMAddPackage(NAME crtsys SOURCE_DIR "${CMAKE_CURRENT_LIST_DIR}/../../..")
include("${crtsys_SOURCE_DIR}/cmake/CrtSys.cmake")

crtsys_add_driver(crtsys_minifilter_swap_buffers_sample
  MINIFILTER
  NTL
  driver/main.cpp
  driver/xor_filter.cpp
)

add_executable(crtsys_minifilter_swap_buffers_sample_app
  app/main.cpp
)

target_include_directories(crtsys_minifilter_swap_buffers_sample
  PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}/shared"
)

target_include_directories(crtsys_minifilter_swap_buffers_sample_app
  PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}/shared"
)

target_compile_options(crtsys_minifilter_swap_buffers_sample
  PRIVATE
    "$<$<COMPILE_LANGUAGE:CXX>:/W4>"
    "$<$<COMPILE_LANGUAGE:CXX>:/WX>"
)

target_compile_options(crtsys_minifilter_swap_buffers_sample_app
  PRIVATE
    "$<$<COMPILE_LANGUAGE:CXX>:/W4>"
    "$<$<COMPILE_LANGUAGE:CXX>:/WX>"
    "$<$<COMPILE_LANGUAGE:CXX>:/EHsc>"
    "$<$<AND:$<COMPILE_LANGUAGE:CXX>,$<CONFIG:Debug>>:/MTd>"
    "$<$<AND:$<COMPILE_LANGUAGE:CXX>,$<CONFIG:Release>>:/MT>"
)

include("${crtsys_SOURCE_DIR}/cmake/std-cxx.cmake")
set_std_cxx_latest(crtsys_minifilter_swap_buffers_sample)
set_std_cxx_latest(crtsys_minifilter_swap_buffers_sample_app)
