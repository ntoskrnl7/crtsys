cmake_minimum_required(VERSION 3.14 FATAL_ERROR)

project(crtsys_wfp_kernel_http3_inspection LANGUAGES C CXX)

include("${CMAKE_CURRENT_LIST_DIR}/../../../../cmake/CPM.cmake")
CPMAddPackage(NAME crtsys SOURCE_DIR "${CMAKE_CURRENT_LIST_DIR}/../../../..")
include("${crtsys_SOURCE_DIR}/cmake/CrtSys.cmake")

crtsys_add_driver(crtsys_wfp_kernel_http3_inspection
  WFP
  NTL
  KERNEL_MSQUIC
  KERNEL_CONTENT_CODECS
  driver/main.cpp
)
add_executable(crtsys_wfp_kernel_http3_inspection_controller
  app/main.cpp
  app/controller.cpp
)
target_include_directories(crtsys_wfp_kernel_http3_inspection PRIVATE
  "${CMAKE_CURRENT_LIST_DIR}/shared"
  "${CMAKE_CURRENT_LIST_DIR}/../../shared"
)
target_include_directories(crtsys_wfp_kernel_http3_inspection_controller PRIVATE
  "${crtsys_SOURCE_DIR}/include"
  "${CMAKE_CURRENT_LIST_DIR}/app"
  "${CMAKE_CURRENT_LIST_DIR}/shared"
  "${CMAKE_CURRENT_LIST_DIR}/../../shared"
)
target_compile_definitions(crtsys_wfp_kernel_http3_inspection PRIVATE
  QUIC_API_ENABLE_PREVIEW_FEATURES
)
target_link_libraries(crtsys_wfp_kernel_http3_inspection_controller PRIVATE
  advapi32 bcrypt crypt32 fwpuclnt ncrypt uuid ws2_32
)
target_compile_options(crtsys_wfp_kernel_http3_inspection PRIVATE
  "$<$<COMPILE_LANGUAGE:CXX>:/W4>"
  "$<$<COMPILE_LANGUAGE:CXX>:/WX>"
)
target_compile_options(crtsys_wfp_kernel_http3_inspection_controller PRIVATE
  "$<$<COMPILE_LANGUAGE:CXX>:/W4>"
  "$<$<COMPILE_LANGUAGE:CXX>:/WX>"
  "$<$<COMPILE_LANGUAGE:CXX>:/EHsc>"
  "$<$<AND:$<COMPILE_LANGUAGE:CXX>,$<CONFIG:Debug>>:/MTd>"
  "$<$<AND:$<COMPILE_LANGUAGE:CXX>,$<CONFIG:Release>>:/MT>"
)
add_custom_command(TARGET crtsys_wfp_kernel_http3_inspection POST_BUILD
  COMMAND "${CMAKE_COMMAND}" -E copy_if_different
          "${CMAKE_CURRENT_LIST_DIR}/crtsys_wfp_kernel_http3_inspection.inf"
          "$<TARGET_FILE_DIR:crtsys_wfp_kernel_http3_inspection>"
)
include("${crtsys_SOURCE_DIR}/cmake/std-cxx.cmake")
set_std_cxx_latest(crtsys_wfp_kernel_http3_inspection)
set_std_cxx_latest(crtsys_wfp_kernel_http3_inspection_controller)

include(CTest)
if(BUILD_TESTING)
  set(crtsys_wfp_kernel_http3_fixture_dir
      "${crtsys_SOURCE_DIR}/test/wfp/runtime/fixtures/kernel/http3-inspection")
  add_executable(crtsys_wfp_kernel_http3_inspection_acceptance
    "${crtsys_wfp_kernel_http3_fixture_dir}/main.cpp"
    "${crtsys_wfp_kernel_http3_fixture_dir}/msquic_client.cpp"
  )
  target_include_directories(
    crtsys_wfp_kernel_http3_inspection_acceptance PRIVATE
      "${crtsys_SOURCE_DIR}/include"
      "${crtsys_wfp_kernel_http3_fixture_dir}"
      "${crtsys_SOURCE_DIR}/test/wfp/runtime/fixtures"
  )
  target_compile_definitions(
    crtsys_wfp_kernel_http3_inspection_acceptance PRIVATE
      QUIC_API_ENABLE_PREVIEW_FEATURES)
  target_link_libraries(
    crtsys_wfp_kernel_http3_inspection_acceptance PRIVATE
      crtsys_ntl_msquic_headers
      crtsys_ntl_content_codecs
      ws2_32
  )
  target_compile_options(
    crtsys_wfp_kernel_http3_inspection_acceptance PRIVATE
      "$<$<COMPILE_LANGUAGE:CXX>:/W4>"
      "$<$<COMPILE_LANGUAGE:CXX>:/WX>"
      "$<$<COMPILE_LANGUAGE:CXX>:/EHsc>"
      "$<$<AND:$<COMPILE_LANGUAGE:CXX>,$<CONFIG:Debug>>:/MTd>"
      "$<$<AND:$<COMPILE_LANGUAGE:CXX>,$<CONFIG:Release>>:/MT>"
  )
  set_std_cxx_latest(crtsys_wfp_kernel_http3_inspection_acceptance)

  add_executable(crtsys_wfp_kernel_http3_inspection_policy_contracts
    test/http3_policy_contracts.cpp
  )
  target_include_directories(
    crtsys_wfp_kernel_http3_inspection_policy_contracts PRIVATE
      "${crtsys_SOURCE_DIR}/include"
      "${CMAKE_CURRENT_LIST_DIR}/driver"
      "${CMAKE_CURRENT_LIST_DIR}/../../shared"
  )
  target_link_libraries(
    crtsys_wfp_kernel_http3_inspection_policy_contracts PRIVATE
      crtsys_ntl_content_codecs
  )
  target_compile_options(
    crtsys_wfp_kernel_http3_inspection_policy_contracts PRIVATE
      "$<$<COMPILE_LANGUAGE:CXX>:/W4>"
      "$<$<COMPILE_LANGUAGE:CXX>:/WX>"
      "$<$<COMPILE_LANGUAGE:CXX>:/EHsc>"
      "$<$<AND:$<COMPILE_LANGUAGE:CXX>,$<CONFIG:Debug>>:/MTd>"
      "$<$<AND:$<COMPILE_LANGUAGE:CXX>,$<CONFIG:Release>>:/MT>"
  )
  set_std_cxx_latest(crtsys_wfp_kernel_http3_inspection_policy_contracts)
  add_test(NAME crtsys_wfp_kernel_http3_inspection_policy_contracts
           COMMAND crtsys_wfp_kernel_http3_inspection_policy_contracts)
endif()
