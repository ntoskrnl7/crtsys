cmake_minimum_required(VERSION 3.14 FATAL_ERROR)

project(crtsys_wfp_datagram_proxy LANGUAGES C CXX)

include("${CMAKE_CURRENT_LIST_DIR}/../../../../cmake/CPM.cmake")

CPMAddPackage(NAME crtsys SOURCE_DIR "${CMAKE_CURRENT_LIST_DIR}/../../../..")
include("${crtsys_SOURCE_DIR}/cmake/CrtSys.cmake")

crtsys_add_driver(crtsys_wfp_datagram_proxy
  WFP
  NTL
  driver/main.cpp
)

add_executable(crtsys_wfp_datagram_proxy_controller
  app/main.cpp
)
add_executable(crtsys_wfp_datagram_proxy_acceptance
  "${crtsys_SOURCE_DIR}/test/wfp/runtime/fixtures/kernel/datagram-proxy/main.cpp"
)

target_include_directories(crtsys_wfp_datagram_proxy
  PRIVATE "${CMAKE_CURRENT_LIST_DIR}/shared"
)
target_include_directories(crtsys_wfp_datagram_proxy_controller
  PRIVATE
    "${crtsys_SOURCE_DIR}/include"
    "${CMAKE_CURRENT_LIST_DIR}/shared"
    "${CMAKE_CURRENT_LIST_DIR}/../../shared"
)
target_include_directories(crtsys_wfp_datagram_proxy_acceptance
  PRIVATE
    "${crtsys_SOURCE_DIR}/include"
    "${crtsys_SOURCE_DIR}/test/wfp/runtime/fixtures/common"
    "${CMAKE_CURRENT_LIST_DIR}/shared"
)

target_link_libraries(crtsys_wfp_datagram_proxy_controller
  PRIVATE fwpuclnt uuid
)
target_link_libraries(crtsys_wfp_datagram_proxy_acceptance
  PRIVATE ws2_32
)
target_link_libraries(crtsys_wfp_datagram_proxy
  WDK::NDIS
)

include(CTest)
if(BUILD_TESTING)
  crtsys_add_driver(crtsys_wfp_datagram_proxy_fragmented_buffer_contract
    WFP
    NTL
    "${crtsys_SOURCE_DIR}/test/wfp/runtime/contracts/kernel/datagram-proxy/fragmented_buffer_contract.cpp"
  )
  target_link_libraries(
    crtsys_wfp_datagram_proxy_fragmented_buffer_contract
    WDK::NDIS
  )
  target_compile_options(
    crtsys_wfp_datagram_proxy_fragmented_buffer_contract
    PRIVATE "$<$<COMPILE_LANGUAGE:CXX>:/W4>"
            "$<$<COMPILE_LANGUAGE:CXX>:/WX>"
  )
  add_custom_command(
    TARGET crtsys_wfp_datagram_proxy_fragmented_buffer_contract POST_BUILD
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different
            "${crtsys_SOURCE_DIR}/test/wfp/runtime/contracts/kernel/datagram-proxy/crtsys_wfp_datagram_proxy_fragmented_buffer_contract.inf"
            "$<TARGET_FILE_DIR:crtsys_wfp_datagram_proxy_fragmented_buffer_contract>"
  )
endif()

foreach(target
    crtsys_wfp_datagram_proxy
    crtsys_wfp_datagram_proxy_controller
    crtsys_wfp_datagram_proxy_acceptance)
  target_compile_options(${target}
    PRIVATE
      "$<$<COMPILE_LANGUAGE:CXX>:/W4>"
      "$<$<COMPILE_LANGUAGE:CXX>:/WX>"
  )
endforeach()

foreach(target
    crtsys_wfp_datagram_proxy_controller
    crtsys_wfp_datagram_proxy_acceptance)
target_compile_options(${target}
  PRIVATE
    "$<$<COMPILE_LANGUAGE:CXX>:/EHsc>"
    "$<$<AND:$<COMPILE_LANGUAGE:CXX>,$<CONFIG:Debug>>:/MTd>"
    "$<$<AND:$<COMPILE_LANGUAGE:CXX>,$<CONFIG:Release>>:/MT>"
)
endforeach()

add_dependencies(crtsys_wfp_datagram_proxy_acceptance
  crtsys_wfp_datagram_proxy_controller)

add_custom_command(TARGET crtsys_wfp_datagram_proxy POST_BUILD
  COMMAND "${CMAKE_COMMAND}" -E copy_if_different
          "${CMAKE_CURRENT_LIST_DIR}/crtsys_wfp_datagram_proxy.inf"
          "$<TARGET_FILE_DIR:crtsys_wfp_datagram_proxy>"
)

include("${crtsys_SOURCE_DIR}/cmake/std-cxx.cmake")
set_std_cxx_latest(crtsys_wfp_datagram_proxy)
set_std_cxx_latest(crtsys_wfp_datagram_proxy_controller)
set_std_cxx_latest(crtsys_wfp_datagram_proxy_acceptance)
if(BUILD_TESTING)
  set_std_cxx_latest(
    crtsys_wfp_datagram_proxy_fragmented_buffer_contract)
endif()
