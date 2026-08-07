cmake_minimum_required(VERSION 3.20 FATAL_ERROR)

project(crtsys_wfp_browser_https_inspection LANGUAGES C CXX)

include("${CMAKE_CURRENT_LIST_DIR}/../../../../cmake/CPM.cmake")

CPMAddPackage(NAME crtsys SOURCE_DIR "${CMAKE_CURRENT_LIST_DIR}/../../../..")
include("${crtsys_SOURCE_DIR}/cmake/CrtSys.cmake")
include("${crtsys_SOURCE_DIR}/cmake/NtlContentCodecs.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/cmake/Http3Dependencies.cmake")

if(CMAKE_GENERATOR_PLATFORM MATCHES "^ARM64")
  set(crtsys_wfp_contract_architecture _ARM64_)
elseif(CMAKE_GENERATOR_PLATFORM MATCHES "^ARM")
  set(crtsys_wfp_contract_architecture _ARM_)
elseif(CMAKE_SIZEOF_VOID_P EQUAL 8)
  set(crtsys_wfp_contract_architecture _AMD64_)
else()
  set(crtsys_wfp_contract_architecture _X86_)
endif()

crtsys_add_driver(crtsys_wfp_browser_https_inspection
  WFP
  NTL
  driver/main.cpp
)

add_executable(crtsys_wfp_browser_https_inspection_controller
  app/browser_log.cpp
  app/browser_policy.cpp
  app/browser_policy_diagnostics.cpp
  app/browser_proxy.cpp
  app/browser_runtime.cpp
  app/browser_tunnels.cpp
  app/main.cpp
)

add_executable(crtsys_wfp_browser_https_inspection_http3_proxy_service
  app/browser_log.cpp
  app/browser_policy.cpp
  app/http3_inspection.cpp
  app/http3_live_proxy.cpp
  app/http3_origin.cpp
  app/http3_proxy_service.cpp
  app/http3_service_main.cpp
)

include(CTest)
if(BUILD_TESTING)
  add_executable(crtsys_ntl_bidirectional_relay_contracts
    test/bidirectional_relay_contracts.cpp)
  add_executable(crtsys_ntl_http1_inspection_contracts
    test/http1_inspection_contracts.cpp
    test/support/http1_inspection_support.cpp)
  add_executable(crtsys_ntl_http1_adapter_contracts
    test/http1_adapter_contracts.cpp
    app/browser_log.cpp
    app/browser_proxy.cpp
    app/browser_tunnels.cpp)
  add_executable(crtsys_ntl_http2_adapter_contracts
    test/http2_adapter_contracts.cpp
    app/browser_log.cpp
    app/browser_proxy.cpp
    app/browser_tunnels.cpp)
  add_executable(crtsys_ntl_http2_proxy_connection_contracts
    test/http2_proxy_connection_contracts.cpp)
  add_executable(crtsys_ntl_http3_inspection_proxy_contracts
    test/http3_inspection_proxy_contracts.cpp)
  add_executable(crtsys_ntl_http3_proxy_connection_contracts
    test/http3_proxy_connection_contracts.cpp)
  add_executable(crtsys_ntl_http3_origin_fallback_contracts
    test/http3_origin_fallback_contracts.cpp)
  add_executable(crtsys_ntl_browser_http_policy_contracts
    test/browser_http_policy_contracts.cpp)
  add_executable(crtsys_ntl_browser_log_privacy_contracts
    test/browser_log_privacy_contracts.cpp
    app/browser_log.cpp)

  foreach(target
      crtsys_ntl_bidirectional_relay_contracts
      crtsys_ntl_http1_inspection_contracts
      crtsys_ntl_http1_adapter_contracts
      crtsys_ntl_http2_adapter_contracts
      crtsys_ntl_http2_proxy_connection_contracts
      crtsys_ntl_http3_inspection_proxy_contracts
      crtsys_ntl_http3_proxy_connection_contracts
      crtsys_ntl_http3_origin_fallback_contracts
      crtsys_ntl_browser_http_policy_contracts
      crtsys_ntl_browser_log_privacy_contracts)
    target_include_directories(${target}
      PRIVATE
        "${crtsys_SOURCE_DIR}/include"
        "${CMAKE_CURRENT_LIST_DIR}/app"
        "${CMAKE_CURRENT_LIST_DIR}/test/support"
        "${CMAKE_CURRENT_LIST_DIR}/../../shared")
    target_compile_options(${target}
      PRIVATE
        "$<$<COMPILE_LANGUAGE:CXX>:/W4>"
        "$<$<COMPILE_LANGUAGE:CXX>:/WX>"
        "$<$<COMPILE_LANGUAGE:CXX>:/EHsc>"
        "$<$<AND:$<COMPILE_LANGUAGE:CXX>,$<CONFIG:Debug>>:/MTd>"
        "$<$<AND:$<COMPILE_LANGUAGE:CXX>,$<CONFIG:Release>>:/MT>")
  endforeach()

  target_link_libraries(crtsys_ntl_bidirectional_relay_contracts
    PRIVATE ws2_32)
  target_link_libraries(crtsys_ntl_http1_inspection_contracts
    PRIVATE crtsys_ntl_content_codecs)
  target_link_libraries(crtsys_ntl_http1_adapter_contracts
    PRIVATE advapi32 bcrypt crypt32 ncrypt secur32 ws2_32
            crtsys_ntl_content_codecs)
  target_link_libraries(crtsys_ntl_http2_adapter_contracts
    PRIVATE advapi32 bcrypt crypt32 ncrypt secur32 ws2_32
            crtsys_ntl_content_codecs)
  target_link_libraries(crtsys_ntl_http3_inspection_proxy_contracts
    PRIVATE crtsys_ntl_content_codecs)
  target_link_libraries(crtsys_ntl_browser_http_policy_contracts
    PRIVATE crtsys_ntl_content_codecs)

  foreach(target
      crtsys_ntl_http1_inspection_contracts
      crtsys_ntl_http1_adapter_contracts
      crtsys_ntl_http2_adapter_contracts
      crtsys_ntl_http2_proxy_connection_contracts
      crtsys_ntl_http3_inspection_proxy_contracts
      crtsys_ntl_http3_proxy_connection_contracts
      crtsys_ntl_browser_http_policy_contracts
      crtsys_ntl_browser_log_privacy_contracts)
    target_compile_definitions(${target}
      PRIVATE ${crtsys_wfp_contract_architecture})
  endforeach()

  foreach(target
      crtsys_ntl_bidirectional_relay_contracts
      crtsys_ntl_http1_inspection_contracts
      crtsys_ntl_http1_adapter_contracts
      crtsys_ntl_http2_adapter_contracts
      crtsys_ntl_http2_proxy_connection_contracts
      crtsys_ntl_http3_inspection_proxy_contracts
      crtsys_ntl_http3_proxy_connection_contracts
      crtsys_ntl_http3_origin_fallback_contracts
      crtsys_ntl_browser_http_policy_contracts
      crtsys_ntl_browser_log_privacy_contracts)
    add_test(NAME ${target} COMMAND ${target})
  endforeach()
endif()

option(CRTSYS_BUILD_WFP_BROWSER_RUNTIME_FIXTURES
  "Build the separately sourced browser HTTPS runtime traffic fixtures" ON)
if(CRTSYS_BUILD_WFP_BROWSER_RUNTIME_FIXTURES)
  add_subdirectory(
    "${crtsys_SOURCE_DIR}/test/wfp/runtime/fixtures/user/browser-https-inspection"
    "${CMAKE_CURRENT_BINARY_DIR}/runtime-fixtures")
endif()

target_include_directories(crtsys_wfp_browser_https_inspection
  PRIVATE "${CMAKE_CURRENT_LIST_DIR}/shared")
target_include_directories(crtsys_wfp_browser_https_inspection_controller
  PRIVATE
    "${crtsys_SOURCE_DIR}/include"
    "${CMAKE_CURRENT_LIST_DIR}/../../shared"
    "${CMAKE_CURRENT_LIST_DIR}/shared")
target_include_directories(
  crtsys_wfp_browser_https_inspection_http3_proxy_service
  PRIVATE
    "${crtsys_SOURCE_DIR}/include"
    "${msh3_SOURCE_DIR}/msquic/src/inc"
    "${CMAKE_CURRENT_LIST_DIR}/app"
    "${CMAKE_CURRENT_LIST_DIR}/../../shared"
    "${CMAKE_CURRENT_LIST_DIR}/shared")
target_link_libraries(crtsys_wfp_browser_https_inspection
  WDK::NDIS)
target_compile_options(crtsys_wfp_browser_https_inspection PRIVATE
  "$<$<COMPILE_LANGUAGE:CXX>:/bigobj>")
target_link_libraries(crtsys_wfp_browser_https_inspection_controller
  PRIVATE
    advapi32 bcrypt crypt32 fwpuclnt ncrypt secur32 uuid ws2_32 winhttp
    crtsys_ntl_content_codecs)
target_link_libraries(
  crtsys_wfp_browser_https_inspection_http3_proxy_service
  PRIVATE
    advapi32 bcrypt crypt32 fwpuclnt ncrypt secur32 uuid ws2_32 winhttp
    crtsys_ntl_content_codecs msquic)

foreach(target
    crtsys_wfp_browser_https_inspection
    crtsys_wfp_browser_https_inspection_controller
    crtsys_wfp_browser_https_inspection_http3_proxy_service)
  target_compile_options(${target}
    PRIVATE
      "$<$<COMPILE_LANGUAGE:CXX>:/W4>"
      "$<$<COMPILE_LANGUAGE:CXX>:/WX>")
endforeach()
target_compile_options(crtsys_wfp_browser_https_inspection_controller
  PRIVATE
    "$<$<COMPILE_LANGUAGE:CXX>:/EHsc>"
    "$<$<AND:$<COMPILE_LANGUAGE:CXX>,$<CONFIG:Debug>>:/MTd>"
    "$<$<AND:$<COMPILE_LANGUAGE:CXX>,$<CONFIG:Release>>:/MT>")
target_compile_options(
  crtsys_wfp_browser_https_inspection_http3_proxy_service
  PRIVATE
    "$<$<COMPILE_LANGUAGE:CXX>:/EHsc>"
    "$<$<AND:$<COMPILE_LANGUAGE:CXX>,$<CONFIG:Debug>>:/MTd>"
      "$<$<AND:$<COMPILE_LANGUAGE:CXX>,$<CONFIG:Release>>:/MT>")
target_compile_definitions(
  crtsys_wfp_browser_https_inspection_http3_proxy_service
  PRIVATE ${crtsys_wfp_contract_architecture})

add_custom_command(
  TARGET crtsys_wfp_browser_https_inspection POST_BUILD
  COMMAND "${CMAKE_COMMAND}" -E copy_if_different
          "${CMAKE_CURRENT_LIST_DIR}/crtsys_wfp_browser_https_inspection.inf"
          "$<TARGET_FILE_DIR:crtsys_wfp_browser_https_inspection>")
add_custom_command(
  TARGET crtsys_wfp_browser_https_inspection_http3_proxy_service POST_BUILD
  COMMAND "${CMAKE_COMMAND}" -E copy_if_different
          "$<TARGET_FILE:msquic>"
          "$<TARGET_FILE_DIR:crtsys_wfp_browser_https_inspection_http3_proxy_service>")

include("${crtsys_SOURCE_DIR}/cmake/std-cxx.cmake")
set_std_cxx_latest(crtsys_wfp_browser_https_inspection)
set_std_cxx_latest(crtsys_wfp_browser_https_inspection_controller)
set_std_cxx_latest(
  crtsys_wfp_browser_https_inspection_http3_proxy_service)
if(BUILD_TESTING)
  foreach(target
      crtsys_ntl_bidirectional_relay_contracts
      crtsys_ntl_http1_inspection_contracts
      crtsys_ntl_http1_adapter_contracts
      crtsys_ntl_http2_adapter_contracts
      crtsys_ntl_http2_proxy_connection_contracts
      crtsys_ntl_http3_inspection_proxy_contracts
      crtsys_ntl_http3_proxy_connection_contracts
      crtsys_ntl_http3_origin_fallback_contracts
      crtsys_ntl_browser_http_policy_contracts
      crtsys_ntl_browser_log_privacy_contracts)
    set_std_cxx_latest(${target})
  endforeach()
endif()
