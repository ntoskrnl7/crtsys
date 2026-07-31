cmake_minimum_required(VERSION 3.20 FATAL_ERROR)

project(crtsys_wfp_browser_https_inspection LANGUAGES C CXX)

include("${CMAKE_CURRENT_LIST_DIR}/../../../cmake/CPM.cmake")

CPMAddPackage(NAME crtsys SOURCE_DIR "${CMAKE_CURRENT_LIST_DIR}/../../..")
include("${crtsys_SOURCE_DIR}/cmake/CrtSys.cmake")
include("${crtsys_SOURCE_DIR}/cmake/NtlContentCodecs.cmake")

# MsQuic provides the QUIC/TLS 1.3 transport and msh3 provides the HTTP/3
# request/response adapter used by the managed client and isolated browser
# SPKI transport diagnostic.
# Keep it pinned because this sample is also a reproducible VM acceptance test.
set(CMAKE_POLICY_VERSION_MINIMUM 3.5)
set(MSH3_TOOL OFF CACHE BOOL "" FORCE)
set(MSH3_PING OFF CACHE BOOL "" FORCE)
set(MSH3_TEST OFF CACHE BOOL "" FORCE)
set(QUIC_BUILD_TEST OFF CACHE BOOL "" FORCE)
set(QUIC_BUILD_TOOLS OFF CACHE BOOL "" FORCE)
set(QUIC_ENABLE_LOGGING OFF CACHE BOOL "" FORCE)
set(QUIC_TLS_LIB schannel CACHE STRING "" FORCE)
# MsQuic's Windows datapath needs the published XDP headers. Fetch them
# directly so msh3 does not recursively clone the unused QuicTLS test corpus,
# whose deep paths exceed the default Windows checkout limit on CI runners.
CPMAddPackage(
  NAME msquic_xdp_headers
  GITHUB_REPOSITORY microsoft/xdp-for-windows
  GIT_TAG f23b1fb4d492d9c20bcd7767bba2278f94355df8
  GIT_SUBMODULES ""
  GIT_SUBMODULES_RECURSE FALSE
  DOWNLOAD_ONLY YES
)
set(EXTRA_PLATFORM_INCLUDE_DIRECTORIES
    "${msquic_xdp_headers_SOURCE_DIR}/published/external")
CPMAddPackage(
  NAME msh3
  GITHUB_REPOSITORY nibanks/msh3
  GIT_TAG 92f23c72b50b0714d7cd39850e7b97e8ed1129e1
  GIT_SUBMODULES "ls-qpack;msquic"
  GIT_SUBMODULES_RECURSE FALSE
)

# The pinned msh3 C API does not surface MsQuic's peer-certificate callback.
# Managed clients need that callback so NTL can validate one application-owned
# CA without disabling trust or writing the CA into a machine/browser store.
# Keep this small ABI extension explicit and fail if the pinned source changes.
set(_msh3_public_header "${msh3_SOURCE_DIR}/msh3.h")
file(READ "${_msh3_public_header}" _msh3_public_source)
if(NOT "${_msh3_public_source}" MATCHES
       "MSH3_CREDENTIAL_FLAG_INDICATE_CERTIFICATE_RECEIVED")
  string(REPLACE
    "MSH3_CREDENTIAL_FLAG_REQUIRE_CLIENT_AUTHENTICATION  = 0x00000004,"
    "MSH3_CREDENTIAL_FLAG_REQUIRE_CLIENT_AUTHENTICATION  = 0x00000004,\n    MSH3_CREDENTIAL_FLAG_INDICATE_CERTIFICATE_RECEIVED   = 0x00000008,"
    _msh3_public_source
    "${_msh3_public_source}")
  string(REPLACE
    "MSH3_CONNECTION_EVENT_NEW_REQUEST                       = 4,"
    "MSH3_CONNECTION_EVENT_NEW_REQUEST                       = 4,\n    MSH3_CONNECTION_EVENT_PEER_CERTIFICATE_RECEIVED          = 5,"
    _msh3_public_source
    "${_msh3_public_source}")
  string(REPLACE
    "        struct {\n            MSH3_REQUEST* Request;\n        } NEW_REQUEST;"
    "        struct {\n            MSH3_REQUEST* Request;\n        } NEW_REQUEST;\n        struct {\n            MSH3_CERTIFICATE_CONTEXT* Certificate;\n            void* Chain;\n        } PEER_CERTIFICATE_RECEIVED;"
    _msh3_public_source
    "${_msh3_public_source}")
  if(NOT "${_msh3_public_source}" MATCHES
         "MSH3_CONNECTION_EVENT_PEER_CERTIFICATE_RECEIVED" OR
     NOT "${_msh3_public_source}" MATCHES
         "PEER_CERTIFICATE_RECEIVED;")
    message(FATAL_ERROR
      "The pinned msh3 public certificate-callback layout changed")
  endif()
  file(WRITE
    "${_msh3_public_header}" "${_msh3_public_source}")
endif()

set(_msh3_implementation "${msh3_SOURCE_DIR}/lib/msh3.cpp")
file(READ "${_msh3_implementation}" _msh3_implementation_source)
if(NOT "${_msh3_implementation_source}" MATCHES
       "MSH3_CREDENTIAL_FLAG_INDICATE_CERTIFICATE_RECEIVED")
  string(REPLACE
    "    if (Flags & MSH3_CREDENTIAL_FLAG_REQUIRE_CLIENT_AUTHENTICATION) {\n        QuicFlags |= QUIC_CREDENTIAL_FLAG_REQUIRE_CLIENT_AUTHENTICATION;\n    }"
    "    if (Flags & MSH3_CREDENTIAL_FLAG_REQUIRE_CLIENT_AUTHENTICATION) {\n        QuicFlags |= QUIC_CREDENTIAL_FLAG_REQUIRE_CLIENT_AUTHENTICATION;\n    }\n    if (Flags & MSH3_CREDENTIAL_FLAG_INDICATE_CERTIFICATE_RECEIVED) {\n        QuicFlags |= QUIC_CREDENTIAL_FLAG_INDICATE_CERTIFICATE_RECEIVED;\n    }"
    _msh3_implementation_source
    "${_msh3_implementation_source}")
  string(REPLACE
    "    case QUIC_CONNECTION_EVENT_PEER_STREAM_STARTED:"
    "    case QUIC_CONNECTION_EVENT_PEER_CERTIFICATE_RECEIVED:\n        h3Event.Type = MSH3_CONNECTION_EVENT_PEER_CERTIFICATE_RECEIVED;\n        h3Event.PEER_CERTIFICATE_RECEIVED.Certificate =\n            (MSH3_CERTIFICATE_CONTEXT*)Event->PEER_CERTIFICATE_RECEIVED.Certificate;\n        h3Event.PEER_CERTIFICATE_RECEIVED.Chain =\n            Event->PEER_CERTIFICATE_RECEIVED.Chain;\n        return Callbacks((MSH3_CONNECTION*)this, Context, &h3Event);\n    case QUIC_CONNECTION_EVENT_PEER_STREAM_STARTED:"
    _msh3_implementation_source
    "${_msh3_implementation_source}")
  if(NOT "${_msh3_implementation_source}" MATCHES
         "QUIC_CREDENTIAL_FLAG_INDICATE_CERTIFICATE_RECEIVED" OR
     NOT "${_msh3_implementation_source}" MATCHES
         "QUIC_CONNECTION_EVENT_PEER_CERTIFICATE_RECEIVED")
    message(FATAL_ERROR
      "The pinned msh3 implementation certificate-callback layout changed")
  endif()
  file(WRITE
    "${_msh3_implementation}" "${_msh3_implementation_source}")
endif()

# msh3's default response QPACK buffer is only 256 bytes. That is enough for
# its ping sample but not for bounded forwarding of ordinary browser response
# headers. Keep the pinned source patch explicit and fail configuration if the
# upstream layout changes instead of silently building an undersized adapter.
set(_msh3_internal_header
    "${msh3_SOURCE_DIR}/lib/msh3_internal.hpp")
file(READ "${_msh3_internal_header}" _msh3_internal_source)
if("${_msh3_internal_source}" MATCHES
    "uint8_t HeadersBuffer\\[256\\];")
  string(REPLACE
    "uint8_t HeadersBuffer[256];"
    "uint8_t HeadersBuffer[64 * 1024];"
    _msh3_internal_source
    "${_msh3_internal_source}")
  file(WRITE
    "${_msh3_internal_header}"
    "${_msh3_internal_source}")
elseif(NOT "${_msh3_internal_source}" MATCHES
       "uint8_t HeadersBuffer\\[64 \\* 1024\\];")
  message(FATAL_ERROR
    "The pinned msh3 request-header buffer layout changed")
endif()

# msh3 decodes one request field into a separate 4 KiB staging buffer. Match
# that buffer to this sample's bounded 32 KiB request-header contract so a
# valid cookie or authorization field is not rejected before policy code sees
# it.
if("${_msh3_internal_source}" MATCHES
    "char DecodeBuffer\\[4096\\];")
  string(REPLACE
    "char DecodeBuffer[4096];"
    "char DecodeBuffer[32 * 1024];"
    _msh3_internal_source
    "${_msh3_internal_source}")
  file(WRITE
    "${_msh3_internal_header}"
    "${_msh3_internal_source}")
elseif(NOT "${_msh3_internal_source}" MATCHES
       "char DecodeBuffer\\[32 \\* 1024\\];")
  message(FATAL_ERROR
    "The pinned msh3 decoded-header buffer layout changed")
endif()

crtsys_add_driver(crtsys_wfp_browser_https_inspection
  WFP
  NTL
  driver/main.cpp
)
add_executable(crtsys_wfp_browser_https_inspection_app
  app/browser_log.cpp
  app/browser_policy.cpp
  app/browser_policy_diagnostics.cpp
  app/browser_proxy.cpp
  app/browser_runtime.cpp
  app/http1_inspection.cpp
  app/http1_inspection_support.cpp
  app/http2_inspection.cpp
  app/http3_inspection.cpp
  app/http3_controlled.cpp
  app/http3_live_proxy.cpp
  app/http3_origin.cpp
  app/main.cpp
)
add_executable(crtsys_ntl_managed_http3_client
  app/managed_http3_client.cpp
)
include(CTest)
if(BUILD_TESTING)
  add_executable(crtsys_ntl_bidirectional_relay_contracts
    test/bidirectional_relay_contracts.cpp
  )
  add_executable(crtsys_ntl_http1_inspection_contracts
    test/http1_inspection_contracts.cpp
    app/http1_inspection_support.cpp
  )
  add_executable(crtsys_ntl_http3_msh3_client_contracts
    test/managed_http3_client_contracts.cpp
  )
  add_executable(crtsys_ntl_http3_inspection_proxy_contracts
    test/http3_inspection_proxy_contracts.cpp
  )
  add_executable(crtsys_ntl_raw_msquic_backend_contracts
    test/raw_msquic_backend_contracts.cpp
  )
  add_executable(crtsys_ntl_raw_msquic_loopback_contracts
    test/raw_msquic_loopback_contracts.cpp
  )
  target_include_directories(
    crtsys_ntl_bidirectional_relay_contracts
    PRIVATE
      "${crtsys_SOURCE_DIR}/include"
      "${CMAKE_CURRENT_LIST_DIR}/app"
      "${CMAKE_CURRENT_LIST_DIR}/../common"
  )
  target_include_directories(
    crtsys_ntl_http1_inspection_contracts
    PRIVATE
      "${crtsys_SOURCE_DIR}/include"
      "${CMAKE_CURRENT_LIST_DIR}/app"
      "${CMAKE_CURRENT_LIST_DIR}/../common"
  )
  target_include_directories(
    crtsys_ntl_http3_msh3_client_contracts
    PRIVATE
      "${crtsys_SOURCE_DIR}/include"
  )
  target_include_directories(
    crtsys_ntl_http3_inspection_proxy_contracts
    PRIVATE
      "${crtsys_SOURCE_DIR}/include"
  )
  target_include_directories(
    crtsys_ntl_raw_msquic_backend_contracts
    PRIVATE
      "${crtsys_SOURCE_DIR}/include"
      "${msquic_SOURCE_DIR}/src/inc"
  )
  target_include_directories(
    crtsys_ntl_raw_msquic_loopback_contracts
    PRIVATE
      "${crtsys_SOURCE_DIR}/include"
      "${CMAKE_CURRENT_LIST_DIR}/../common"
      "${msquic_SOURCE_DIR}/src/inc"
  )
  target_link_libraries(
    crtsys_ntl_bidirectional_relay_contracts
    PRIVATE ws2_32
  )
  target_link_libraries(
    crtsys_ntl_http1_inspection_contracts
    PRIVATE
      crtsys_ntl_content_codecs
  )
  target_link_libraries(
    crtsys_ntl_http3_inspection_proxy_contracts
    PRIVATE
      crtsys_ntl_content_codecs
  )
  if(CMAKE_GENERATOR_PLATFORM MATCHES "^ARM64")
    set(crtsys_wfp_contract_architecture _ARM64_)
  elseif(CMAKE_GENERATOR_PLATFORM MATCHES "^ARM")
    set(crtsys_wfp_contract_architecture _ARM_)
  elseif(CMAKE_SIZEOF_VOID_P EQUAL 8)
    set(crtsys_wfp_contract_architecture _AMD64_)
  else()
    set(crtsys_wfp_contract_architecture _X86_)
  endif()
  target_compile_definitions(
    crtsys_ntl_http3_inspection_proxy_contracts
    PRIVATE ${crtsys_wfp_contract_architecture})
  target_compile_definitions(
    crtsys_ntl_http1_inspection_contracts
    PRIVATE ${crtsys_wfp_contract_architecture})
  target_link_libraries(
    crtsys_ntl_http3_msh3_client_contracts
    PRIVATE
      crypt32
      msh3
  )
  target_link_libraries(
    crtsys_ntl_raw_msquic_backend_contracts
    PRIVATE
      msquic
  )
  target_link_libraries(
    crtsys_ntl_raw_msquic_loopback_contracts
    PRIVATE
      advapi32
      crypt32
      msquic
  )
  target_compile_definitions(
    crtsys_ntl_raw_msquic_loopback_contracts
    PRIVATE QUIC_API_ENABLE_PREVIEW_FEATURES)
  target_compile_options(
    crtsys_ntl_bidirectional_relay_contracts
    PRIVATE
      "$<$<COMPILE_LANGUAGE:CXX>:/W4>"
      "$<$<COMPILE_LANGUAGE:CXX>:/WX>"
      "$<$<COMPILE_LANGUAGE:CXX>:/EHsc>"
      "$<$<AND:$<COMPILE_LANGUAGE:CXX>,$<CONFIG:Debug>>:/MTd>"
      "$<$<AND:$<COMPILE_LANGUAGE:CXX>,$<CONFIG:Release>>:/MT>"
  )
  target_compile_options(
    crtsys_ntl_http1_inspection_contracts
    PRIVATE
      "$<$<COMPILE_LANGUAGE:CXX>:/W4>"
      "$<$<COMPILE_LANGUAGE:CXX>:/WX>"
      "$<$<COMPILE_LANGUAGE:CXX>:/EHsc>"
      "$<$<AND:$<COMPILE_LANGUAGE:CXX>,$<CONFIG:Debug>>:/MTd>"
      "$<$<AND:$<COMPILE_LANGUAGE:CXX>,$<CONFIG:Release>>:/MT>"
  )
  target_compile_options(
    crtsys_ntl_http3_msh3_client_contracts
    PRIVATE
      "$<$<COMPILE_LANGUAGE:CXX>:/W4>"
      "$<$<COMPILE_LANGUAGE:CXX>:/WX>"
      "$<$<COMPILE_LANGUAGE:CXX>:/EHsc>"
      "$<$<AND:$<COMPILE_LANGUAGE:CXX>,$<CONFIG:Debug>>:/MTd>"
      "$<$<AND:$<COMPILE_LANGUAGE:CXX>,$<CONFIG:Release>>:/MT>"
  )
  target_compile_options(
    crtsys_ntl_http3_inspection_proxy_contracts
    PRIVATE
      "$<$<COMPILE_LANGUAGE:CXX>:/W4>"
      "$<$<COMPILE_LANGUAGE:CXX>:/WX>"
      "$<$<COMPILE_LANGUAGE:CXX>:/EHsc>"
      "$<$<AND:$<COMPILE_LANGUAGE:CXX>,$<CONFIG:Debug>>:/MTd>"
      "$<$<AND:$<COMPILE_LANGUAGE:CXX>,$<CONFIG:Release>>:/MT>"
  )
  target_compile_options(
    crtsys_ntl_raw_msquic_backend_contracts
    PRIVATE
      "$<$<COMPILE_LANGUAGE:CXX>:/W4>"
      "$<$<COMPILE_LANGUAGE:CXX>:/WX>"
      "$<$<COMPILE_LANGUAGE:CXX>:/EHsc>"
      "$<$<AND:$<COMPILE_LANGUAGE:CXX>,$<CONFIG:Debug>>:/MTd>"
      "$<$<AND:$<COMPILE_LANGUAGE:CXX>,$<CONFIG:Release>>:/MT>"
  )
  target_compile_options(
    crtsys_ntl_raw_msquic_loopback_contracts
    PRIVATE
      "$<$<COMPILE_LANGUAGE:CXX>:/W4>"
      "$<$<COMPILE_LANGUAGE:CXX>:/WX>"
      "$<$<COMPILE_LANGUAGE:CXX>:/EHsc>"
      "$<$<AND:$<COMPILE_LANGUAGE:CXX>,$<CONFIG:Debug>>:/MTd>"
      "$<$<AND:$<COMPILE_LANGUAGE:CXX>,$<CONFIG:Release>>:/MT>"
  )
  add_custom_command(
    TARGET crtsys_ntl_http3_msh3_client_contracts POST_BUILD
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different
            "$<TARGET_FILE:msh3>"
            "$<TARGET_FILE_DIR:crtsys_ntl_http3_msh3_client_contracts>"
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different
            "$<TARGET_FILE:msquic>"
            "$<TARGET_FILE_DIR:crtsys_ntl_http3_msh3_client_contracts>"
  )
  add_custom_command(
    TARGET crtsys_ntl_raw_msquic_loopback_contracts POST_BUILD
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different
            "$<TARGET_FILE:msquic>"
            "$<TARGET_FILE_DIR:crtsys_ntl_raw_msquic_loopback_contracts>"
  )
  add_test(
    NAME crtsys_ntl_bidirectional_relay_contracts
    COMMAND crtsys_ntl_bidirectional_relay_contracts
  )
  add_test(
    NAME crtsys_ntl_http1_inspection_contracts
    COMMAND crtsys_ntl_http1_inspection_contracts
  )
  add_test(
    NAME crtsys_ntl_http3_msh3_client_contracts
    COMMAND crtsys_ntl_http3_msh3_client_contracts
  )
  add_test(
    NAME crtsys_ntl_http3_inspection_proxy_contracts
    COMMAND crtsys_ntl_http3_inspection_proxy_contracts
  )
  add_test(
    NAME crtsys_ntl_raw_msquic_backend_contracts
    COMMAND crtsys_ntl_raw_msquic_backend_contracts
  )
  add_test(
    NAME crtsys_ntl_raw_msquic_loopback_contracts
    COMMAND crtsys_ntl_raw_msquic_loopback_contracts
  )
  add_test(
    NAME crtsys_ntl_controlled_http3_end_to_end
    COMMAND powershell.exe -NoProfile -ExecutionPolicy Bypass
            -File
            "${CMAKE_CURRENT_LIST_DIR}/../../../test/wfp/runtime/https-live/Start-ControlledHttp3EndToEnd.ps1"
            -PackageRoot
            "$<TARGET_FILE_DIR:crtsys_wfp_browser_https_inspection_app>"
            -LogDirectory
            "${CMAKE_CURRENT_BINARY_DIR}/controlled-http3-ctest"
            -Concurrency 4
  )
  set_tests_properties(
    crtsys_ntl_controlled_http3_end_to_end
    PROPERTIES TIMEOUT 180)
endif()

target_include_directories(crtsys_wfp_browser_https_inspection
  PRIVATE "${CMAKE_CURRENT_LIST_DIR}/shared"
)
target_include_directories(crtsys_wfp_browser_https_inspection_app
  PRIVATE
    "${crtsys_SOURCE_DIR}/include"
    "${CMAKE_CURRENT_LIST_DIR}/../common"
    "${CMAKE_CURRENT_LIST_DIR}/shared"
)
target_link_libraries(
  crtsys_wfp_browser_https_inspection WDK::NDIS
)
target_link_libraries(
  crtsys_wfp_browser_https_inspection_app
  PRIVATE
    advapi32 bcrypt crypt32 fwpuclnt ncrypt secur32 ws2_32
    winhttp
    crtsys_ntl_content_codecs
    msh3
)
target_link_libraries(
  crtsys_ntl_managed_http3_client
  PRIVATE
    crypt32
    winhttp
    msh3
)
target_include_directories(
  crtsys_ntl_managed_http3_client
  PRIVATE
    "${crtsys_SOURCE_DIR}/include"
)

foreach(target
    crtsys_wfp_browser_https_inspection
    crtsys_wfp_browser_https_inspection_app
    crtsys_ntl_managed_http3_client)
  target_compile_options(${target}
    PRIVATE
      "$<$<COMPILE_LANGUAGE:CXX>:/W4>"
      "$<$<COMPILE_LANGUAGE:CXX>:/WX>"
  )
endforeach()
target_compile_options(crtsys_wfp_browser_https_inspection_app
  PRIVATE
    "$<$<COMPILE_LANGUAGE:CXX>:/EHsc>"
    "$<$<AND:$<COMPILE_LANGUAGE:CXX>,$<CONFIG:Debug>>:/MTd>"
    "$<$<AND:$<COMPILE_LANGUAGE:CXX>,$<CONFIG:Release>>:/MT>"
)
target_compile_options(crtsys_ntl_managed_http3_client
  PRIVATE
    "$<$<COMPILE_LANGUAGE:CXX>:/EHsc>"
    "$<$<AND:$<COMPILE_LANGUAGE:CXX>,$<CONFIG:Debug>>:/MTd>"
    "$<$<AND:$<COMPILE_LANGUAGE:CXX>,$<CONFIG:Release>>:/MT>"
)

add_custom_command(
  TARGET crtsys_wfp_browser_https_inspection POST_BUILD
  COMMAND "${CMAKE_COMMAND}" -E copy_if_different
          "${CMAKE_CURRENT_LIST_DIR}/crtsys_wfp_browser_https_inspection.inf"
          "$<TARGET_FILE_DIR:crtsys_wfp_browser_https_inspection>"
)
add_custom_command(
  TARGET crtsys_wfp_browser_https_inspection_app POST_BUILD
  COMMAND "${CMAKE_COMMAND}" -E copy_if_different
          "$<TARGET_FILE:msh3>"
          "$<TARGET_FILE_DIR:crtsys_wfp_browser_https_inspection_app>"
  COMMAND "${CMAKE_COMMAND}" -E copy_if_different
          "$<TARGET_FILE:msquic>"
          "$<TARGET_FILE_DIR:crtsys_wfp_browser_https_inspection_app>"
)
add_custom_command(
  TARGET crtsys_ntl_managed_http3_client POST_BUILD
  COMMAND "${CMAKE_COMMAND}" -E copy_if_different
          "$<TARGET_FILE:msh3>"
          "$<TARGET_FILE_DIR:crtsys_ntl_managed_http3_client>"
  COMMAND "${CMAKE_COMMAND}" -E copy_if_different
          "$<TARGET_FILE:msquic>"
          "$<TARGET_FILE_DIR:crtsys_ntl_managed_http3_client>"
)

include("${crtsys_SOURCE_DIR}/cmake/std-cxx.cmake")
set_std_cxx_latest(crtsys_wfp_browser_https_inspection)
set_std_cxx_latest(crtsys_wfp_browser_https_inspection_app)
set_std_cxx_latest(crtsys_ntl_managed_http3_client)
if(BUILD_TESTING)
  set_std_cxx_latest(crtsys_ntl_bidirectional_relay_contracts)
  set_std_cxx_latest(crtsys_ntl_http1_inspection_contracts)
  set_std_cxx_latest(crtsys_ntl_http3_msh3_client_contracts)
  set_std_cxx_latest(crtsys_ntl_http3_inspection_proxy_contracts)
  set_std_cxx_latest(crtsys_ntl_raw_msquic_backend_contracts)
  set_std_cxx_latest(crtsys_ntl_raw_msquic_loopback_contracts)
endif()
