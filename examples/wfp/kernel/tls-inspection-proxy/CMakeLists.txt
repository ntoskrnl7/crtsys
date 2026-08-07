cmake_minimum_required(VERSION 3.14 FATAL_ERROR)

project(crtsys_wfp_kernel_tls_inspection_proxy LANGUAGES C CXX)

include("${CMAKE_CURRENT_LIST_DIR}/../../../../cmake/CPM.cmake")
CPMAddPackage(NAME crtsys SOURCE_DIR "${CMAKE_CURRENT_LIST_DIR}/../../../..")
include("${crtsys_SOURCE_DIR}/cmake/CrtSys.cmake")

crtsys_add_driver(crtsys_wfp_kernel_tls_inspection_proxy
  WFP
  NTL
  driver/inspection_policy.cpp
  driver/main.cpp
)
add_executable(crtsys_wfp_kernel_tls_inspection_proxy_controller app/main.cpp)
add_executable(crtsys_wfp_kernel_tls_inspection_proxy_acceptance
  ../../../../test/wfp/runtime/fixtures/kernel/tls-inspection-proxy/main.cpp
)
target_include_directories(crtsys_wfp_kernel_tls_inspection_proxy PRIVATE
  "${CMAKE_CURRENT_LIST_DIR}/shared"
  "${CMAKE_CURRENT_LIST_DIR}/../common"
  "${CMAKE_CURRENT_LIST_DIR}/../../shared"
)
target_include_directories(crtsys_wfp_kernel_tls_inspection_proxy_controller PRIVATE
  "${crtsys_SOURCE_DIR}/include"
  "${CMAKE_CURRENT_LIST_DIR}/shared"
  "${CMAKE_CURRENT_LIST_DIR}/../../shared"
)
target_include_directories(crtsys_wfp_kernel_tls_inspection_proxy_acceptance PRIVATE
  "${crtsys_SOURCE_DIR}/include"
  "${CMAKE_CURRENT_LIST_DIR}/shared"
  "${CMAKE_CURRENT_LIST_DIR}/../../shared"
  "${crtsys_SOURCE_DIR}/test/wfp/runtime/fixtures/common"
)
target_link_libraries(crtsys_wfp_kernel_tls_inspection_proxy
  WDK::KSECDD
  WDK::NETIO
)
target_link_libraries(crtsys_wfp_kernel_tls_inspection_proxy_controller PRIVATE
  advapi32 bcrypt crypt32 fwpuclnt ncrypt secur32 uuid ws2_32
)
target_link_libraries(crtsys_wfp_kernel_tls_inspection_proxy_acceptance PRIVATE
  bcrypt crypt32 ncrypt secur32 ws2_32
)
add_dependencies(crtsys_wfp_kernel_tls_inspection_proxy_acceptance
  crtsys_wfp_kernel_tls_inspection_proxy_controller)
foreach(target
    crtsys_wfp_kernel_tls_inspection_proxy
    crtsys_wfp_kernel_tls_inspection_proxy_controller
    crtsys_wfp_kernel_tls_inspection_proxy_acceptance)
  target_compile_options(${target} PRIVATE
    "$<$<COMPILE_LANGUAGE:CXX>:/W4>"
    "$<$<COMPILE_LANGUAGE:CXX>:/WX>"
  )
endforeach()
foreach(user_target
    crtsys_wfp_kernel_tls_inspection_proxy_controller
    crtsys_wfp_kernel_tls_inspection_proxy_acceptance)
target_compile_options(${user_target} PRIVATE
  "$<$<COMPILE_LANGUAGE:CXX>:/EHsc>"
  "$<$<AND:$<COMPILE_LANGUAGE:CXX>,$<CONFIG:Debug>>:/MTd>"
  "$<$<AND:$<COMPILE_LANGUAGE:CXX>,$<CONFIG:Release>>:/MT>"
)
endforeach()
add_custom_command(TARGET crtsys_wfp_kernel_tls_inspection_proxy POST_BUILD
  COMMAND "${CMAKE_COMMAND}" -E copy_if_different
          "${CMAKE_CURRENT_LIST_DIR}/crtsys_wfp_kernel_tls_inspection_proxy.inf"
          "$<TARGET_FILE_DIR:crtsys_wfp_kernel_tls_inspection_proxy>"
)
include("${crtsys_SOURCE_DIR}/cmake/std-cxx.cmake")
set_std_cxx_latest(crtsys_wfp_kernel_tls_inspection_proxy)
set_std_cxx_latest(crtsys_wfp_kernel_tls_inspection_proxy_controller)
set_std_cxx_latest(crtsys_wfp_kernel_tls_inspection_proxy_acceptance)

include(CTest)
if(BUILD_TESTING)
  add_executable(crtsys_wfp_kernel_tls_inspection_policy_contracts
    driver/inspection_policy.cpp
    test/inspection_policy_contracts.cpp
  )
  target_include_directories(
    crtsys_wfp_kernel_tls_inspection_policy_contracts PRIVATE
      "${crtsys_SOURCE_DIR}/include"
      "${CMAKE_CURRENT_LIST_DIR}/driver"
      "${CMAKE_CURRENT_LIST_DIR}/../../shared"
  )
  target_compile_options(
    crtsys_wfp_kernel_tls_inspection_policy_contracts PRIVATE
      "$<$<COMPILE_LANGUAGE:CXX>:/W4>"
      "$<$<COMPILE_LANGUAGE:CXX>:/WX>"
      "$<$<COMPILE_LANGUAGE:CXX>:/EHsc>"
      "$<$<AND:$<COMPILE_LANGUAGE:CXX>,$<CONFIG:Debug>>:/MTd>"
      "$<$<AND:$<COMPILE_LANGUAGE:CXX>,$<CONFIG:Release>>:/MT>"
  )
  set_std_cxx_latest(crtsys_wfp_kernel_tls_inspection_policy_contracts)
  add_test(NAME crtsys_wfp_kernel_tls_inspection_policy_contracts
           COMMAND crtsys_wfp_kernel_tls_inspection_policy_contracts)
endif()
