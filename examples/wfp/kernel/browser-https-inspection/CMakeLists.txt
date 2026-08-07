cmake_minimum_required(VERSION 3.14 FATAL_ERROR)

project(crtsys_wfp_kernel_browser_https_inspection LANGUAGES C CXX)

include("${CMAKE_CURRENT_LIST_DIR}/../../../../cmake/CPM.cmake")
CPMAddPackage(NAME crtsys SOURCE_DIR "${CMAKE_CURRENT_LIST_DIR}/../../../..")
include("${crtsys_SOURCE_DIR}/cmake/CrtSys.cmake")
include("${crtsys_SOURCE_DIR}/cmake/NtlContentCodecs.cmake")
crtsys_add_ntl_kernel_content_codecs()

crtsys_add_driver(crtsys_wfp_kernel_browser_https_inspection
  WFP
  NTL
  KERNEL_MSQUIC
  KERNEL_CONTENT_CODECS
  driver/main.cpp
  driver/request_capture.hpp
  driver/tcp_common.hpp
  driver/tcp_service.hpp
  driver/tcp_session.hpp
  driver/http1_proxy.hpp
  driver/websocket_proxy.hpp
  driver/http3_service.hpp
  driver/webtransport_capsule_stream.hpp
)
target_include_directories(crtsys_wfp_kernel_browser_https_inspection PRIVATE
  "${CMAKE_CURRENT_LIST_DIR}/shared"
  "${CMAKE_CURRENT_LIST_DIR}/../../shared"
)

function(configure_kernel_browser_user_target target)
  target_include_directories(${target} PRIVATE
    "${crtsys_SOURCE_DIR}/include"
    "${CMAKE_CURRENT_LIST_DIR}/app"
    "${CMAKE_CURRENT_LIST_DIR}/shared"
    "${CMAKE_CURRENT_LIST_DIR}/../../shared"
  )
  target_compile_definitions(${target} PRIVATE
    NOMINMAX
    WIN32_LEAN_AND_MEAN
  )
  target_compile_options(${target} PRIVATE
    "$<$<COMPILE_LANGUAGE:CXX>:/W4>"
    "$<$<COMPILE_LANGUAGE:CXX>:/WX>"
    "$<$<COMPILE_LANGUAGE:CXX>:/EHsc>"
    "$<$<AND:$<COMPILE_LANGUAGE:CXX>,$<CONFIG:Debug>>:/MTd>"
    "$<$<AND:$<COMPILE_LANGUAGE:CXX>,$<NOT:$<CONFIG:Debug>>>:/MT>"
  )
  target_link_libraries(${target} PRIVATE fwpuclnt uuid)
  set_std_cxx_latest(${target})
endfunction()

function(package_kernel_browser_driver target)
  add_dependencies(${target} crtsys_wfp_kernel_browser_https_inspection)
  add_custom_command(TARGET ${target}
    POST_BUILD
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different
            "$<TARGET_FILE:crtsys_wfp_kernel_browser_https_inspection>"
            "$<TARGET_FILE_DIR:${target}>"
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different
            "${CMAKE_CURRENT_LIST_DIR}/crtsys_wfp_kernel_browser_https_inspection.inf"
            "$<TARGET_FILE_DIR:${target}>"
  )
endfunction()

include("${crtsys_SOURCE_DIR}/cmake/std-cxx.cmake")

add_executable(crtsys_wfp_kernel_browser_https_inspection_controller
  app/main.cpp
  app/controller.cpp
  app/control_server.cpp
  app/browser_policy.cpp
  app/capture_log.cpp
  app/certificate_authority.cpp
  app/certificate_store.cpp
  app/identity_provisioner.cpp
  app/kernel_tls_service.cpp
  app/managed_policy.cpp
)
configure_kernel_browser_user_target(
  crtsys_wfp_kernel_browser_https_inspection_controller)
package_kernel_browser_driver(
  crtsys_wfp_kernel_browser_https_inspection_controller)
target_compile_definitions(crtsys_wfp_kernel_browser_https_inspection PRIVATE
  QUIC_API_ENABLE_PREVIEW_FEATURES
)
target_compile_options(crtsys_wfp_kernel_browser_https_inspection PRIVATE
  "$<$<COMPILE_LANGUAGE:CXX>:/bigobj>"
)
target_link_libraries(crtsys_wfp_kernel_browser_https_inspection
  WDK::KSECDD
  WDK::NDIS
)
target_link_libraries(
  crtsys_wfp_kernel_browser_https_inspection_controller PRIVATE
  advapi32 bcrypt crypt32 ncrypt secur32 ws2_32
)
set_std_cxx_latest(crtsys_wfp_kernel_browser_https_inspection)

include(CTest)
if(BUILD_TESTING)
  set(kernel_browser_acceptance_source_dir
    "${crtsys_SOURCE_DIR}/test/wfp/runtime/fixtures/kernel/browser-https-inspection")
  add_executable(crtsys_wfp_kernel_browser_https_inspection_acceptance
    "${kernel_browser_acceptance_source_dir}/main.cpp"
    "${kernel_browser_acceptance_source_dir}/managed_acceptance.cpp"
    "${kernel_browser_acceptance_source_dir}/acceptance_controller.cpp"
    "${kernel_browser_acceptance_source_dir}/managed_tcp_scenario.cpp"
    "${kernel_browser_acceptance_source_dir}/managed_http1_scenario.inl"
    "${kernel_browser_acceptance_source_dir}/managed_http2_scenario.inl"
    "${kernel_browser_acceptance_source_dir}/managed_http3_fallback_scenario.inl"
    "${kernel_browser_acceptance_source_dir}/managed_websocket_scenario.inl"
    "${kernel_browser_acceptance_source_dir}/managed_dynamic_sni_scenario.inl"
    "${kernel_browser_acceptance_source_dir}/managed_http3_client.cpp"
    "${kernel_browser_acceptance_source_dir}/managed_http3_origin.cpp"
    app/capture_log.cpp
    app/certificate_authority.cpp
    app/certificate_store.cpp
    app/identity_provisioner.cpp
  )
  target_include_directories(
    crtsys_wfp_kernel_browser_https_inspection_acceptance PRIVATE
    "${kernel_browser_acceptance_source_dir}")
  configure_kernel_browser_user_target(
    crtsys_wfp_kernel_browser_https_inspection_acceptance)
  target_compile_definitions(
    crtsys_wfp_kernel_browser_https_inspection_acceptance PRIVATE
    QUIC_API_ENABLE_PREVIEW_FEATURES)
  package_kernel_browser_driver(
    crtsys_wfp_kernel_browser_https_inspection_acceptance)
  target_link_libraries(
    crtsys_wfp_kernel_browser_https_inspection_acceptance PRIVATE
    crtsys_ntl_content_codecs
    crtsys_ntl_msquic_headers
    advapi32 bcrypt crypt32 iphlpapi ncrypt secur32 ws2_32
  )

  wdk_add_library(crtsys_wfp_kernel_browser_http3_origin_contracts
    EXTENDED_CPP_FEATURES
    WINVER 0x0A00
    STATIC
    test/http3_origin_client_contracts.cpp
    test/configuration_transaction_contracts.cpp
  )
  crtsys_apply_driver_settings(
    crtsys_wfp_kernel_browser_http3_origin_contracts
    "${crtsys_SOURCE_DIR}"
    FALSE FALSE FALSE
  )
  target_include_directories(
    crtsys_wfp_kernel_browser_http3_origin_contracts PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}/driver"
    "${CMAKE_CURRENT_LIST_DIR}/shared"
    "${CMAKE_CURRENT_LIST_DIR}/../../shared"
  )
  target_compile_definitions(
    crtsys_wfp_kernel_browser_http3_origin_contracts PRIVATE
    NTDDI_VERSION=NTDDI_WIN10_VB
    QUIC_API_ENABLE_PREVIEW_FEATURES
  )
  if(WDK_PLATFORM MATCHES "^ARM")
    target_compile_definitions(
      crtsys_wfp_kernel_browser_http3_origin_contracts PRIVATE NDIS630)
  else()
    target_compile_definitions(
      crtsys_wfp_kernel_browser_http3_origin_contracts PRIVATE NDIS60)
  endif()
  target_compile_definitions(
    crtsys_wfp_kernel_browser_http3_origin_contracts PRIVATE
    CRTSYS_USE_WFP
    NDIS_SUPPORT_NDIS6
  )
  target_compile_options(
    crtsys_wfp_kernel_browser_http3_origin_contracts PRIVATE
    "$<$<COMPILE_LANGUAGE:CXX>:/W4>"
    "$<$<COMPILE_LANGUAGE:CXX>:/WX>"
    "$<$<COMPILE_LANGUAGE:CXX>:/FS>"
  )
  target_link_libraries(
    crtsys_wfp_kernel_browser_http3_origin_contracts PRIVATE
    crtsys
    crtsys_ntl_msquic_headers
    crtsys_ntl_kernel_content_codecs
    WDK::FWPKCLNT
    WDK::KSECDD
    WDK::NETIO
  )
  set_std_cxx_latest(crtsys_wfp_kernel_browser_http3_origin_contracts)

  wdk_add_library(crtsys_wfp_kernel_browser_http3_fallback_contracts
    EXTENDED_CPP_FEATURES
    WINVER 0x0A00
    STATIC
    test/http3_origin_fallback_contracts.cpp
  )
  crtsys_apply_driver_settings(
    crtsys_wfp_kernel_browser_http3_fallback_contracts
    "${crtsys_SOURCE_DIR}"
    FALSE FALSE FALSE
  )
  target_include_directories(
    crtsys_wfp_kernel_browser_http3_fallback_contracts PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}/driver"
  )
  target_compile_definitions(
    crtsys_wfp_kernel_browser_http3_fallback_contracts PRIVATE
    NTDDI_VERSION=NTDDI_WIN10_VB
    QUIC_API_ENABLE_PREVIEW_FEATURES
  )
  target_compile_options(
    crtsys_wfp_kernel_browser_http3_fallback_contracts PRIVATE
    "$<$<COMPILE_LANGUAGE:CXX>:/W4>"
    "$<$<COMPILE_LANGUAGE:CXX>:/WX>"
    "$<$<COMPILE_LANGUAGE:CXX>:/FS>"
  )
  target_link_libraries(
    crtsys_wfp_kernel_browser_http3_fallback_contracts PRIVATE
    crtsys
    crtsys_ntl_msquic_headers
    crtsys_ntl_kernel_content_codecs
    WDK::KSECDD
    WDK::NETIO
  )
  set_std_cxx_latest(crtsys_wfp_kernel_browser_http3_fallback_contracts)

  function(configure_kernel_browser_evidence_contract target)
    target_include_directories(${target} PRIVATE
      "${crtsys_SOURCE_DIR}/include"
      "${CMAKE_CURRENT_LIST_DIR}/app"
      "${CMAKE_CURRENT_LIST_DIR}/shared"
      "${kernel_browser_acceptance_source_dir}"
    )
    target_compile_definitions(${target} PRIVATE
      NOMINMAX WIN32_LEAN_AND_MEAN
    )
    target_compile_options(${target} PRIVATE
      "$<$<COMPILE_LANGUAGE:CXX>:/W4>"
      "$<$<COMPILE_LANGUAGE:CXX>:/WX>"
      "$<$<COMPILE_LANGUAGE:CXX>:/EHsc>"
    )
    set_std_cxx_latest(${target})
  endfunction()

  add_executable(crtsys_wfp_kernel_browser_http1_evidence_contracts
    test/http1_evidence_contracts.cpp)
  configure_kernel_browser_evidence_contract(
    crtsys_wfp_kernel_browser_http1_evidence_contracts)
  add_test(
    NAME crtsys_wfp_kernel_browser_http1_evidence_contracts
    COMMAND crtsys_wfp_kernel_browser_http1_evidence_contracts)

  add_executable(crtsys_wfp_kernel_browser_http2_evidence_contracts
    test/http2_evidence_contracts.cpp)
  configure_kernel_browser_evidence_contract(
    crtsys_wfp_kernel_browser_http2_evidence_contracts)
  add_test(
    NAME crtsys_wfp_kernel_browser_http2_evidence_contracts
    COMMAND crtsys_wfp_kernel_browser_http2_evidence_contracts)

  add_executable(crtsys_wfp_kernel_browser_http3_evidence_contracts
    test/http3_evidence_contracts.cpp)
  configure_kernel_browser_evidence_contract(
    crtsys_wfp_kernel_browser_http3_evidence_contracts)
  add_test(
    NAME crtsys_wfp_kernel_browser_http3_evidence_contracts
    COMMAND crtsys_wfp_kernel_browser_http3_evidence_contracts)

  add_executable(crtsys_wfp_kernel_browser_service_info_abi_contracts
    test/service_info_abi_contracts.cpp)
  configure_kernel_browser_evidence_contract(
    crtsys_wfp_kernel_browser_service_info_abi_contracts)
  add_test(
    NAME crtsys_wfp_kernel_browser_service_info_abi_contracts
    COMMAND crtsys_wfp_kernel_browser_service_info_abi_contracts)

  add_executable(crtsys_wfp_kernel_browser_request_capture_contracts
    test/request_capture_contracts.cpp)
  configure_kernel_browser_evidence_contract(
    crtsys_wfp_kernel_browser_request_capture_contracts)
  target_include_directories(
    crtsys_wfp_kernel_browser_request_capture_contracts PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}/driver")
  add_test(
    NAME crtsys_wfp_kernel_browser_request_capture_contracts
    COMMAND crtsys_wfp_kernel_browser_request_capture_contracts)

  add_executable(crtsys_wfp_kernel_browser_capture_log_privacy_contracts
    test/capture_log_privacy_contracts.cpp
    app/capture_log.cpp)
  configure_kernel_browser_evidence_contract(
    crtsys_wfp_kernel_browser_capture_log_privacy_contracts)
  add_test(
    NAME crtsys_wfp_kernel_browser_capture_log_privacy_contracts
    COMMAND crtsys_wfp_kernel_browser_capture_log_privacy_contracts)

  add_executable(crtsys_wfp_kernel_browser_http_policy_contracts
    test/browser_http_policy_contracts.cpp)
  configure_kernel_browser_evidence_contract(
    crtsys_wfp_kernel_browser_http_policy_contracts)
  target_include_directories(
    crtsys_wfp_kernel_browser_http_policy_contracts PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}/../../shared")
  if(CMAKE_GENERATOR_PLATFORM MATCHES "^ARM64")
    target_compile_definitions(
      crtsys_wfp_kernel_browser_http_policy_contracts PRIVATE _ARM64_)
  elseif(CMAKE_GENERATOR_PLATFORM MATCHES "^ARM")
    target_compile_definitions(
      crtsys_wfp_kernel_browser_http_policy_contracts PRIVATE _ARM_)
  elseif(CMAKE_SIZEOF_VOID_P EQUAL 8)
    target_compile_definitions(
      crtsys_wfp_kernel_browser_http_policy_contracts PRIVATE _AMD64_)
  else()
    target_compile_definitions(
      crtsys_wfp_kernel_browser_http_policy_contracts PRIVATE _X86_)
  endif()
  target_link_libraries(
    crtsys_wfp_kernel_browser_http_policy_contracts PRIVATE
    crtsys_ntl_content_codecs)
  add_test(
    NAME crtsys_wfp_kernel_browser_http_policy_contracts
    COMMAND crtsys_wfp_kernel_browser_http_policy_contracts)

  add_executable(crtsys_wfp_kernel_browser_webtransport_capsule_contracts
    test/webtransport_capsule_stream_contracts.cpp
  )
  target_include_directories(
    crtsys_wfp_kernel_browser_webtransport_capsule_contracts PRIVATE
    "${crtsys_SOURCE_DIR}/include"
    "${CMAKE_CURRENT_LIST_DIR}/driver"
  )
  target_compile_definitions(
    crtsys_wfp_kernel_browser_webtransport_capsule_contracts PRIVATE
    NOMINMAX WIN32_LEAN_AND_MEAN
  )
  target_compile_options(
    crtsys_wfp_kernel_browser_webtransport_capsule_contracts PRIVATE
    "$<$<COMPILE_LANGUAGE:CXX>:/W4>"
    "$<$<COMPILE_LANGUAGE:CXX>:/WX>"
    "$<$<COMPILE_LANGUAGE:CXX>:/EHsc>"
  )
  set_std_cxx_latest(
    crtsys_wfp_kernel_browser_webtransport_capsule_contracts)
  add_test(
    NAME crtsys_wfp_kernel_browser_webtransport_capsule_contracts
    COMMAND crtsys_wfp_kernel_browser_webtransport_capsule_contracts
  )
endif()
