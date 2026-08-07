cmake_minimum_required(VERSION 3.14 FATAL_ERROR)

project(crtsys_wfp_tcp_content_filter LANGUAGES C CXX)

enable_testing()

include("${CMAKE_CURRENT_LIST_DIR}/../../../../cmake/CPM.cmake")

CPMAddPackage(NAME crtsys SOURCE_DIR "${CMAKE_CURRENT_LIST_DIR}/../../../..")
include("${crtsys_SOURCE_DIR}/cmake/CrtSys.cmake")

crtsys_add_driver(crtsys_wfp_tcp_content_filter
  WFP
  NTL
  driver/main.cpp
)

add_executable(crtsys_wfp_tcp_content_filter_policy_service
  app/main.cpp
)

add_executable(crtsys_wfp_tcp_content_filter_acceptance
  ../../../../test/wfp/runtime/fixtures/user/tcp-content-filter/main.cpp
)

add_executable(crtsys_wfp_content_filter_record_contracts
  ../../shared/content_filter_record_contracts.cpp
)

target_include_directories(crtsys_wfp_tcp_content_filter
  PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}/shared"
    "${CMAKE_CURRENT_LIST_DIR}/../../shared"
)
target_include_directories(crtsys_wfp_tcp_content_filter_policy_service
  PRIVATE
    "${crtsys_SOURCE_DIR}/include"
    "${CMAKE_CURRENT_LIST_DIR}/shared"
    "${CMAKE_CURRENT_LIST_DIR}/../../shared"
)
target_include_directories(crtsys_wfp_tcp_content_filter_acceptance
  PRIVATE
    "${crtsys_SOURCE_DIR}/include"
    "${CMAKE_CURRENT_LIST_DIR}/shared"
    "${CMAKE_CURRENT_LIST_DIR}/../../shared"
    "${crtsys_SOURCE_DIR}/test/wfp/runtime/fixtures/common"
)
target_include_directories(crtsys_wfp_content_filter_record_contracts
  PRIVATE
    "${crtsys_SOURCE_DIR}/include"
    "${CMAKE_CURRENT_LIST_DIR}/../../shared"
)

target_link_libraries(crtsys_wfp_tcp_content_filter WDK::NDIS)
target_link_libraries(crtsys_wfp_tcp_content_filter_policy_service
  PRIVATE advapi32 fwpuclnt uuid ws2_32
)
target_link_libraries(crtsys_wfp_tcp_content_filter_acceptance
  PRIVATE ws2_32
)
add_dependencies(crtsys_wfp_tcp_content_filter_acceptance
  crtsys_wfp_tcp_content_filter_policy_service)

foreach(target
    crtsys_wfp_tcp_content_filter
    crtsys_wfp_tcp_content_filter_policy_service
    crtsys_wfp_tcp_content_filter_acceptance
    crtsys_wfp_content_filter_record_contracts)
  target_compile_options(${target}
    PRIVATE
      "$<$<COMPILE_LANGUAGE:CXX>:/W4>"
      "$<$<COMPILE_LANGUAGE:CXX>:/WX>"
  )
endforeach()

target_compile_options(crtsys_wfp_content_filter_record_contracts
  PRIVATE
    "$<$<COMPILE_LANGUAGE:CXX>:/EHsc>"
    "$<$<AND:$<COMPILE_LANGUAGE:CXX>,$<CONFIG:Debug>>:/MTd>"
    "$<$<AND:$<COMPILE_LANGUAGE:CXX>,$<CONFIG:Release>>:/MT>"
)

add_test(NAME crtsys_wfp_content_filter_record_contracts
  COMMAND crtsys_wfp_content_filter_record_contracts)

target_compile_options(crtsys_wfp_tcp_content_filter_policy_service
  PRIVATE
    "$<$<COMPILE_LANGUAGE:CXX>:/EHsc>"
    "$<$<AND:$<COMPILE_LANGUAGE:CXX>,$<CONFIG:Debug>>:/MTd>"
    "$<$<AND:$<COMPILE_LANGUAGE:CXX>,$<CONFIG:Release>>:/MT>"
)
target_compile_options(crtsys_wfp_tcp_content_filter_acceptance
  PRIVATE
    "$<$<COMPILE_LANGUAGE:CXX>:/EHsc>"
    "$<$<AND:$<COMPILE_LANGUAGE:CXX>,$<CONFIG:Debug>>:/MTd>"
    "$<$<AND:$<COMPILE_LANGUAGE:CXX>,$<CONFIG:Release>>:/MT>"
)

add_custom_command(TARGET crtsys_wfp_tcp_content_filter POST_BUILD
  COMMAND "${CMAKE_COMMAND}" -E copy_if_different
          "${CMAKE_CURRENT_LIST_DIR}/crtsys_wfp_tcp_content_filter.inf"
          "$<TARGET_FILE_DIR:crtsys_wfp_tcp_content_filter>"
)

include("${crtsys_SOURCE_DIR}/cmake/std-cxx.cmake")
set_std_cxx_latest(crtsys_wfp_tcp_content_filter)
set_std_cxx_latest(crtsys_wfp_tcp_content_filter_policy_service)
set_std_cxx_latest(crtsys_wfp_tcp_content_filter_acceptance)
set_std_cxx_latest(crtsys_wfp_content_filter_record_contracts)
