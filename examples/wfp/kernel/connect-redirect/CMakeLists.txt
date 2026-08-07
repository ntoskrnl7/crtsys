cmake_minimum_required(VERSION 3.14 FATAL_ERROR)

project(crtsys_wfp_kernel_connect_redirect LANGUAGES C CXX)

include("${CMAKE_CURRENT_LIST_DIR}/../../../../cmake/CPM.cmake")
CPMAddPackage(NAME crtsys SOURCE_DIR "${CMAKE_CURRENT_LIST_DIR}/../../../..")
include("${crtsys_SOURCE_DIR}/cmake/CrtSys.cmake")

crtsys_add_driver(crtsys_wfp_kernel_connect_redirect
  WFP
  NTL
  driver/main.cpp
)
add_executable(crtsys_wfp_kernel_connect_redirect_controller app/main.cpp)
add_executable(crtsys_wfp_kernel_connect_redirect_acceptance
  "${crtsys_SOURCE_DIR}/test/wfp/runtime/fixtures/kernel/connect-redirect/main.cpp"
)
target_include_directories(crtsys_wfp_kernel_connect_redirect
  PRIVATE "${CMAKE_CURRENT_LIST_DIR}/shared"
)
target_include_directories(crtsys_wfp_kernel_connect_redirect_controller
  PRIVATE "${crtsys_SOURCE_DIR}/include" "${CMAKE_CURRENT_LIST_DIR}/shared"
          "${CMAKE_CURRENT_LIST_DIR}/../../shared"
)
target_include_directories(crtsys_wfp_kernel_connect_redirect_acceptance
  PRIVATE "${crtsys_SOURCE_DIR}/test/wfp/runtime/fixtures"
)
target_link_libraries(crtsys_wfp_kernel_connect_redirect
  WDK::NETIO
)
target_link_libraries(crtsys_wfp_kernel_connect_redirect_controller
  PRIVATE fwpuclnt uuid ws2_32
)
target_link_libraries(crtsys_wfp_kernel_connect_redirect_acceptance
  PRIVATE ws2_32
)
foreach(target
    crtsys_wfp_kernel_connect_redirect
    crtsys_wfp_kernel_connect_redirect_controller
    crtsys_wfp_kernel_connect_redirect_acceptance)
  target_compile_options(${target} PRIVATE
    "$<$<COMPILE_LANGUAGE:CXX>:/W4>"
    "$<$<COMPILE_LANGUAGE:CXX>:/WX>"
  )
endforeach()
target_compile_options(crtsys_wfp_kernel_connect_redirect_controller PRIVATE
  "$<$<COMPILE_LANGUAGE:CXX>:/EHsc>"
  "$<$<AND:$<COMPILE_LANGUAGE:CXX>,$<CONFIG:Debug>>:/MTd>"
  "$<$<AND:$<COMPILE_LANGUAGE:CXX>,$<CONFIG:Release>>:/MT>"
)
target_compile_options(crtsys_wfp_kernel_connect_redirect_acceptance PRIVATE
  "$<$<COMPILE_LANGUAGE:CXX>:/EHsc>"
  "$<$<AND:$<COMPILE_LANGUAGE:CXX>,$<CONFIG:Debug>>:/MTd>"
  "$<$<AND:$<COMPILE_LANGUAGE:CXX>,$<CONFIG:Release>>:/MT>"
)
set_target_properties(
  crtsys_wfp_kernel_connect_redirect_controller
  crtsys_wfp_kernel_connect_redirect_acceptance
  PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/$<CONFIG>"
)
add_custom_command(TARGET crtsys_wfp_kernel_connect_redirect POST_BUILD
  COMMAND "${CMAKE_COMMAND}" -E copy_if_different
          "${CMAKE_CURRENT_LIST_DIR}/crtsys_wfp_kernel_connect_redirect.inf"
          "$<TARGET_FILE_DIR:crtsys_wfp_kernel_connect_redirect>"
)
include("${crtsys_SOURCE_DIR}/cmake/std-cxx.cmake")
set_std_cxx_latest(crtsys_wfp_kernel_connect_redirect)
set_std_cxx_latest(crtsys_wfp_kernel_connect_redirect_controller)
set_std_cxx_latest(crtsys_wfp_kernel_connect_redirect_acceptance)
