cmake_minimum_required(VERSION 3.14 FATAL_ERROR)

project(crtsys_wfp_kernel_tcp_content_filter LANGUAGES C CXX)

include("${CMAKE_CURRENT_LIST_DIR}/../../../../cmake/CPM.cmake")
CPMAddPackage(NAME crtsys SOURCE_DIR "${CMAKE_CURRENT_LIST_DIR}/../../../..")
include("${crtsys_SOURCE_DIR}/cmake/CrtSys.cmake")

crtsys_add_driver(crtsys_wfp_kernel_tcp_content_filter
  WFP
  NTL
  driver/main.cpp
)
add_executable(crtsys_wfp_kernel_tcp_content_filter_controller app/main.cpp)
add_executable(crtsys_wfp_kernel_tcp_content_filter_acceptance
  ../../../../test/wfp/runtime/fixtures/kernel/tcp-content-filter/main.cpp)

target_include_directories(crtsys_wfp_kernel_tcp_content_filter
  PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}/shared"
    "${CMAKE_CURRENT_LIST_DIR}/../../shared"
)
target_include_directories(crtsys_wfp_kernel_tcp_content_filter_controller
  PRIVATE
    "${crtsys_SOURCE_DIR}/include"
    "${CMAKE_CURRENT_LIST_DIR}/shared"
    "${CMAKE_CURRENT_LIST_DIR}/../../shared"
)
target_include_directories(crtsys_wfp_kernel_tcp_content_filter_acceptance
  PRIVATE
    "${crtsys_SOURCE_DIR}/include"
    "${CMAKE_CURRENT_LIST_DIR}/shared"
    "${CMAKE_CURRENT_LIST_DIR}/../../shared"
    "${crtsys_SOURCE_DIR}/test/wfp/runtime/fixtures/common"
)
target_link_libraries(crtsys_wfp_kernel_tcp_content_filter_controller
  PRIVATE fwpuclnt uuid
)
target_link_libraries(crtsys_wfp_kernel_tcp_content_filter_acceptance
  PRIVATE ws2_32
)
add_dependencies(crtsys_wfp_kernel_tcp_content_filter_acceptance
  crtsys_wfp_kernel_tcp_content_filter_controller)

foreach(target
    crtsys_wfp_kernel_tcp_content_filter
    crtsys_wfp_kernel_tcp_content_filter_controller
    crtsys_wfp_kernel_tcp_content_filter_acceptance)
  target_compile_options(${target} PRIVATE
    "$<$<COMPILE_LANGUAGE:CXX>:/W4>"
    "$<$<COMPILE_LANGUAGE:CXX>:/WX>"
  )
endforeach()
target_compile_options(crtsys_wfp_kernel_tcp_content_filter_controller PRIVATE
  "$<$<COMPILE_LANGUAGE:CXX>:/EHsc>"
  "$<$<AND:$<COMPILE_LANGUAGE:CXX>,$<CONFIG:Debug>>:/MTd>"
  "$<$<AND:$<COMPILE_LANGUAGE:CXX>,$<CONFIG:Release>>:/MT>"
)
target_compile_options(crtsys_wfp_kernel_tcp_content_filter_acceptance PRIVATE
  "$<$<COMPILE_LANGUAGE:CXX>:/EHsc>"
  "$<$<AND:$<COMPILE_LANGUAGE:CXX>,$<CONFIG:Debug>>:/MTd>"
  "$<$<AND:$<COMPILE_LANGUAGE:CXX>,$<CONFIG:Release>>:/MT>"
)

add_custom_command(TARGET crtsys_wfp_kernel_tcp_content_filter POST_BUILD
  COMMAND "${CMAKE_COMMAND}" -E copy_if_different
          "${CMAKE_CURRENT_LIST_DIR}/crtsys_wfp_kernel_tcp_content_filter.inf"
          "$<TARGET_FILE_DIR:crtsys_wfp_kernel_tcp_content_filter>"
)

include("${crtsys_SOURCE_DIR}/cmake/std-cxx.cmake")
set_std_cxx_latest(crtsys_wfp_kernel_tcp_content_filter)
set_std_cxx_latest(crtsys_wfp_kernel_tcp_content_filter_controller)
set_std_cxx_latest(crtsys_wfp_kernel_tcp_content_filter_acceptance)
