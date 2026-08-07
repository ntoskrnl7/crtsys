cmake_minimum_required(VERSION 3.20 FATAL_ERROR)

project(crtsys_wfp_browser_https_inspection_acceptance LANGUAGES C CXX)

include("${CMAKE_CURRENT_LIST_DIR}/../../../../../../cmake/CPM.cmake")

CPMAddPackage(
  NAME crtsys
  SOURCE_DIR "${CMAKE_CURRENT_LIST_DIR}/../../../../../..")
include("${crtsys_SOURCE_DIR}/cmake/CrtSys.cmake")
include("${crtsys_SOURCE_DIR}/cmake/NtlContentCodecs.cmake")

include("${crtsys_SOURCE_DIR}/examples/wfp/user/browser-https-inspection/cmake/Http3Dependencies.cmake")

set(crtsys_browser_https_example_dir
    "${crtsys_SOURCE_DIR}/examples/wfp/user/browser-https-inspection")

add_executable(crtsys_wfp_browser_https_inspection_acceptance
  acceptance_main.cpp
  http3_controlled.cpp
  "${crtsys_browser_https_example_dir}/app/browser_log.cpp"
  "${crtsys_browser_https_example_dir}/app/http3_inspection.cpp"
  "${crtsys_browser_https_example_dir}/app/http3_live_proxy.cpp"
  "${crtsys_browser_https_example_dir}/app/http3_origin.cpp")
set(crtsys_browser_managed_client_target
    crtsys_wfp_browser_managed_client_acceptance)
set(crtsys_browser_managed_client_output
    crtsys_wfp_browser_https_inspection_managed_client_acceptance)
add_executable(${crtsys_browser_managed_client_target}
  managed_http3_client.cpp)
set_target_properties(${crtsys_browser_managed_client_target} PROPERTIES
  OUTPUT_NAME ${crtsys_browser_managed_client_output})
add_custom_target(
  crtsys_wfp_browser_https_inspection_managed_client_acceptance
  DEPENDS ${crtsys_browser_managed_client_target})

include(CTest)
if(BUILD_TESTING)
  add_executable(
    crtsys_wfp_browser_https_inspection_msh3_client_acceptance
    managed_http3_client_contracts.cpp)
  add_executable(
    crtsys_wfp_browser_https_inspection_raw_msquic_acceptance
    raw_msquic_backend_contracts.cpp)
  set(crtsys_browser_msquic_loopback_target
      crtsys_wfp_browser_msquic_loopback_acceptance)
  set(crtsys_browser_msquic_loopback_output
      crtsys_wfp_browser_https_inspection_msquic_loopback_acceptance)
  add_executable(${crtsys_browser_msquic_loopback_target}
    raw_msquic_loopback_contracts.cpp)
  set_target_properties(${crtsys_browser_msquic_loopback_target} PROPERTIES
    OUTPUT_NAME ${crtsys_browser_msquic_loopback_output})
  add_custom_target(
    crtsys_wfp_browser_https_inspection_msquic_loopback_acceptance
    DEPENDS ${crtsys_browser_msquic_loopback_target})
endif()

foreach(target
    crtsys_wfp_browser_https_inspection_acceptance
    ${crtsys_browser_managed_client_target})
  set_target_properties(${target} PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")
  target_include_directories(${target}
    PRIVATE
      "${crtsys_SOURCE_DIR}/include"
      "${msh3_SOURCE_DIR}/msquic/src/inc"
      "${crtsys_browser_https_example_dir}/app"
      "${crtsys_browser_https_example_dir}/shared"
      "${crtsys_SOURCE_DIR}/examples/wfp/shared")
  target_compile_options(${target}
    PRIVATE
      "$<$<COMPILE_LANGUAGE:CXX>:/W4>"
      "$<$<COMPILE_LANGUAGE:CXX>:/WX>"
      "$<$<COMPILE_LANGUAGE:CXX>:/EHsc>"
      "$<$<AND:$<COMPILE_LANGUAGE:CXX>,$<CONFIG:Debug>>:/MTd>"
      "$<$<AND:$<COMPILE_LANGUAGE:CXX>,$<CONFIG:Release>>:/MT>")
endforeach()

target_link_libraries(
  crtsys_wfp_browser_https_inspection_acceptance
  PRIVATE
    advapi32 bcrypt crypt32 fwpuclnt ncrypt secur32 uuid ws2_32 winhttp
    crtsys_ntl_content_codecs msh3 msquic)
target_link_libraries(
  ${crtsys_browser_managed_client_target}
  PRIVATE crypt32 winhttp msh3)

foreach(target
    crtsys_wfp_browser_https_inspection_acceptance
    ${crtsys_browser_managed_client_target})
  add_custom_command(
    TARGET ${target} POST_BUILD
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different
            "$<TARGET_FILE:msh3>"
            "$<TARGET_FILE_DIR:${target}>"
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different
            "$<TARGET_FILE:msquic>"
            "$<TARGET_FILE_DIR:${target}>")
endforeach()

if(BUILD_TESTING)
  target_include_directories(
    crtsys_wfp_browser_https_inspection_msh3_client_acceptance
    PRIVATE "${crtsys_SOURCE_DIR}/include")
  target_include_directories(
    crtsys_wfp_browser_https_inspection_raw_msquic_acceptance
    PRIVATE
      "${crtsys_SOURCE_DIR}/include"
      "${msquic_SOURCE_DIR}/src/inc")
  target_include_directories(
    ${crtsys_browser_msquic_loopback_target}
    PRIVATE
      "${crtsys_SOURCE_DIR}/include"
      "${crtsys_SOURCE_DIR}/examples/wfp/shared"
      "${msquic_SOURCE_DIR}/src/inc")

  target_link_libraries(
    crtsys_wfp_browser_https_inspection_msh3_client_acceptance
    PRIVATE crypt32 msh3)
  target_link_libraries(
    crtsys_wfp_browser_https_inspection_raw_msquic_acceptance
    PRIVATE msquic)
  target_link_libraries(
    ${crtsys_browser_msquic_loopback_target}
    PRIVATE advapi32 crypt32 msquic)
  target_compile_definitions(
    ${crtsys_browser_msquic_loopback_target}
    PRIVATE QUIC_API_ENABLE_PREVIEW_FEATURES)

  foreach(target
      crtsys_wfp_browser_https_inspection_msh3_client_acceptance
      crtsys_wfp_browser_https_inspection_raw_msquic_acceptance
      ${crtsys_browser_msquic_loopback_target})
    set_target_properties(${target} PROPERTIES
      RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")
    target_compile_options(${target}
      PRIVATE
        "$<$<COMPILE_LANGUAGE:CXX>:/W4>"
        "$<$<COMPILE_LANGUAGE:CXX>:/WX>"
        "$<$<COMPILE_LANGUAGE:CXX>:/EHsc>"
        "$<$<AND:$<COMPILE_LANGUAGE:CXX>,$<CONFIG:Debug>>:/MTd>"
        "$<$<AND:$<COMPILE_LANGUAGE:CXX>,$<CONFIG:Release>>:/MT>")
  endforeach()
  add_test(
    NAME crtsys_wfp_browser_https_inspection_msh3_client_acceptance
    COMMAND crtsys_wfp_browser_https_inspection_msh3_client_acceptance)
  add_test(
    NAME crtsys_wfp_browser_https_inspection_raw_msquic_acceptance
    COMMAND crtsys_wfp_browser_https_inspection_raw_msquic_acceptance)
  add_test(
    NAME crtsys_wfp_browser_https_inspection_msquic_loopback_acceptance
    COMMAND ${crtsys_browser_msquic_loopback_target})
  set_tests_properties(
    crtsys_wfp_browser_https_inspection_raw_msquic_acceptance
    PROPERTIES TIMEOUT 30)

  add_custom_command(
    TARGET crtsys_wfp_browser_https_inspection_msh3_client_acceptance
    POST_BUILD
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different
            "$<TARGET_FILE:msh3>"
            "$<TARGET_FILE_DIR:crtsys_wfp_browser_https_inspection_msh3_client_acceptance>"
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different
            "$<TARGET_FILE:msquic>"
            "$<TARGET_FILE_DIR:crtsys_wfp_browser_https_inspection_msh3_client_acceptance>")
  add_custom_command(
    TARGET ${crtsys_browser_msquic_loopback_target}
    POST_BUILD
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different
            "$<TARGET_FILE:msquic>"
            "$<TARGET_FILE_DIR:${crtsys_browser_msquic_loopback_target}>")

  add_test(
    NAME crtsys_wfp_browser_https_inspection_controlled_http3_acceptance
    COMMAND crtsys_wfp_browser_https_inspection_acceptance
            --controlled-http3-e2e
            31443
            31444
            "${CMAKE_CURRENT_BINARY_DIR}/controlled-http3-ctest"
            1)
  set_tests_properties(
    crtsys_wfp_browser_https_inspection_controlled_http3_acceptance
    PROPERTIES TIMEOUT 180)
endif()

include("${crtsys_SOURCE_DIR}/cmake/std-cxx.cmake")
set_std_cxx_latest(crtsys_wfp_browser_https_inspection_acceptance)
set_std_cxx_latest(
  ${crtsys_browser_managed_client_target})
if(BUILD_TESTING)
  set_std_cxx_latest(
    crtsys_wfp_browser_https_inspection_msh3_client_acceptance)
  set_std_cxx_latest(
    crtsys_wfp_browser_https_inspection_raw_msquic_acceptance)
  set_std_cxx_latest(
    ${crtsys_browser_msquic_loopback_target})
endif()
