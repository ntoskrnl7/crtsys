cmake_minimum_required(VERSION 3.14 FATAL_ERROR)

project(crtsys_wfp_compile_contracts LANGUAGES C CXX)

enable_testing()

option(CRTSYS_WFP_ENABLE_LIBFUZZER
  "Build the Clang coverage-guided WFP protocol fuzz target" OFF)

include("${CMAKE_CURRENT_LIST_DIR}/../../../cmake/CPM.cmake")

CPMAddPackage(NAME crtsys SOURCE_DIR "${CMAKE_CURRENT_LIST_DIR}/../../..")
include("${crtsys_SOURCE_DIR}/cmake/CrtSys.cmake")
include("${crtsys_SOURCE_DIR}/cmake/NtlContentCodecs.cmake")

crtsys_add_driver(crtsys_wfp_compile_contracts
  WFP
  NTL
  kernel.cpp
)
target_link_libraries(crtsys_wfp_compile_contracts
  WDK::NDIS
)

add_executable(crtsys_wfp_management_contracts
  management.cpp
)

add_executable(crtsys_wfp_scatter_contracts
  scatter.cpp
)

add_executable(crtsys_wfp_async_socket_contracts
  async_socket.cpp
)

add_executable(crtsys_wfp_tls_stream_contracts
  tls_stream.cpp
)

add_executable(crtsys_wfp_inspection_contracts
  inspection.cpp
)

add_executable(crtsys_wfp_protocol_contracts
  protocols.cpp
)

add_executable(crtsys_wfp_http_transform_contracts
  http_transform.cpp
)

add_executable(crtsys_wfp_fuzz_contracts
  fuzz.cpp
)

if(CRTSYS_WFP_ENABLE_LIBFUZZER)
  if(NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    message(FATAL_ERROR
      "CRTSYS_WFP_ENABLE_LIBFUZZER requires Clang with compiler-rt")
  endif()
  add_executable(crtsys_wfp_protocol_libfuzzer
    fuzz.cpp
  )
  target_compile_definitions(crtsys_wfp_protocol_libfuzzer
    PRIVATE CRTSYS_WFP_LIBFUZZER=1)
  target_compile_options(crtsys_wfp_protocol_libfuzzer
    PRIVATE -fsanitize=fuzzer,address -O1 -gline-tables-only /MT)
  target_link_options(crtsys_wfp_protocol_libfuzzer
    PRIVATE -fsanitize=fuzzer,address /INCREMENTAL:NO)
  set_property(TARGET crtsys_wfp_protocol_libfuzzer PROPERTY
    MSVC_RUNTIME_LIBRARY "MultiThreaded")
endif()

add_executable(crtsys_wfp_policy_stress_contracts
  policy_stress.cpp
)

add_executable(crtsys_wfp_browser_http3_contracts
  browser_http3.cpp
  ../../../examples/wfp/browser-https-inspection/app/browser_log.cpp
  ../../../examples/wfp/browser-https-inspection/app/http3_inspection.cpp
)

target_link_libraries(crtsys_wfp_protocol_contracts
  PRIVATE
    crtsys_ntl_content_codecs
)

target_link_libraries(crtsys_wfp_http_transform_contracts
  PRIVATE
    crtsys_ntl_content_codecs
)

target_link_libraries(crtsys_wfp_fuzz_contracts
  PRIVATE
    crtsys_ntl_content_codecs
)

if(CRTSYS_WFP_ENABLE_LIBFUZZER)
  target_link_libraries(crtsys_wfp_protocol_libfuzzer
    PRIVATE crtsys_ntl_content_codecs)
  target_include_directories(crtsys_wfp_protocol_libfuzzer
    PRIVATE "${crtsys_SOURCE_DIR}/include")
endif()

target_link_libraries(crtsys_wfp_browser_http3_contracts
  PRIVATE
    crtsys_ntl_content_codecs
)

target_include_directories(crtsys_wfp_management_contracts
  PRIVATE
    "${crtsys_SOURCE_DIR}/include"
)

target_include_directories(crtsys_wfp_scatter_contracts
  PRIVATE
    "${crtsys_SOURCE_DIR}/include"
)

target_include_directories(crtsys_wfp_async_socket_contracts
  PRIVATE
    "${crtsys_SOURCE_DIR}/include"
)

target_include_directories(crtsys_wfp_tls_stream_contracts
  PRIVATE
    "${crtsys_SOURCE_DIR}/include"
)

target_include_directories(crtsys_wfp_inspection_contracts
  PRIVATE
    "${crtsys_SOURCE_DIR}/include"
)

target_include_directories(crtsys_wfp_protocol_contracts
  PRIVATE
    "${crtsys_SOURCE_DIR}/include"
)

target_include_directories(crtsys_wfp_http_transform_contracts
  PRIVATE
    "${crtsys_SOURCE_DIR}/include"
)

target_include_directories(crtsys_wfp_fuzz_contracts
  PRIVATE
    "${crtsys_SOURCE_DIR}/include"
)

target_include_directories(crtsys_wfp_policy_stress_contracts
  PRIVATE
    "${crtsys_SOURCE_DIR}/include"
)

target_include_directories(crtsys_wfp_browser_http3_contracts
  PRIVATE
    "${crtsys_SOURCE_DIR}/include"
    "${crtsys_SOURCE_DIR}/examples/wfp/common"
    "${crtsys_SOURCE_DIR}/examples/wfp/browser-https-inspection/app"
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
  crtsys_wfp_scatter_contracts
  PRIVATE ${crtsys_wfp_contract_architecture})
target_compile_definitions(
  crtsys_wfp_inspection_contracts
  PRIVATE ${crtsys_wfp_contract_architecture})
target_compile_definitions(
  crtsys_wfp_protocol_contracts
  PRIVATE ${crtsys_wfp_contract_architecture})
target_compile_definitions(
  crtsys_wfp_http_transform_contracts
  PRIVATE ${crtsys_wfp_contract_architecture})
target_compile_definitions(
  crtsys_wfp_policy_stress_contracts
  PRIVATE ${crtsys_wfp_contract_architecture})
target_compile_definitions(
  crtsys_wfp_browser_http3_contracts
  PRIVATE ${crtsys_wfp_contract_architecture})
if(CRTSYS_WFP_ENABLE_LIBFUZZER)
  target_compile_definitions(crtsys_wfp_protocol_libfuzzer
    PRIVATE ${crtsys_wfp_contract_architecture})
endif()

target_link_libraries(crtsys_wfp_management_contracts
  PRIVATE
    advapi32
    fwpuclnt
)

foreach(contract_target
    crtsys_wfp_management_contracts
    crtsys_wfp_scatter_contracts
    crtsys_wfp_async_socket_contracts
    crtsys_wfp_tls_stream_contracts
    crtsys_wfp_inspection_contracts
    crtsys_wfp_protocol_contracts
    crtsys_wfp_http_transform_contracts
    crtsys_wfp_fuzz_contracts
    crtsys_wfp_policy_stress_contracts
    crtsys_wfp_browser_http3_contracts)
  add_test(
    NAME ${contract_target}
    COMMAND ${contract_target}
  )
endforeach()

target_link_libraries(crtsys_wfp_async_socket_contracts
  PRIVATE
    ws2_32
)

target_link_libraries(crtsys_wfp_tls_stream_contracts
  PRIVATE
    advapi32 bcrypt crypt32 ncrypt secur32 ws2_32
)

target_compile_options(crtsys_wfp_compile_contracts
  PRIVATE
    "$<$<COMPILE_LANGUAGE:CXX>:/W4>"
    "$<$<COMPILE_LANGUAGE:CXX>:/WX>"
)

target_compile_options(crtsys_wfp_management_contracts
  PRIVATE
    "$<$<COMPILE_LANGUAGE:CXX>:/W4>"
    "$<$<COMPILE_LANGUAGE:CXX>:/WX>"
    "$<$<COMPILE_LANGUAGE:CXX>:/EHsc>"
    "$<$<AND:$<COMPILE_LANGUAGE:CXX>,$<CONFIG:Debug>>:/MTd>"
    "$<$<AND:$<COMPILE_LANGUAGE:CXX>,$<CONFIG:Release>>:/MT>"
)

target_compile_options(crtsys_wfp_scatter_contracts
  PRIVATE
    "$<$<COMPILE_LANGUAGE:CXX>:/W4>"
    "$<$<COMPILE_LANGUAGE:CXX>:/WX>"
    "$<$<COMPILE_LANGUAGE:CXX>:/EHsc>"
    "$<$<AND:$<COMPILE_LANGUAGE:CXX>,$<CONFIG:Debug>>:/MTd>"
    "$<$<AND:$<COMPILE_LANGUAGE:CXX>,$<CONFIG:Release>>:/MT>"
)

target_compile_options(crtsys_wfp_async_socket_contracts
  PRIVATE
    "$<$<COMPILE_LANGUAGE:CXX>:/W4>"
    "$<$<COMPILE_LANGUAGE:CXX>:/WX>"
    "$<$<COMPILE_LANGUAGE:CXX>:/EHsc>"
    "$<$<AND:$<COMPILE_LANGUAGE:CXX>,$<CONFIG:Debug>>:/MTd>"
    "$<$<AND:$<COMPILE_LANGUAGE:CXX>,$<CONFIG:Release>>:/MT>"
)

target_compile_options(crtsys_wfp_tls_stream_contracts
  PRIVATE
    "$<$<COMPILE_LANGUAGE:CXX>:/W4>"
    "$<$<COMPILE_LANGUAGE:CXX>:/WX>"
    "$<$<COMPILE_LANGUAGE:CXX>:/EHsc>"
    "$<$<AND:$<COMPILE_LANGUAGE:CXX>,$<CONFIG:Debug>>:/MTd>"
    "$<$<AND:$<COMPILE_LANGUAGE:CXX>,$<CONFIG:Release>>:/MT>"
)

target_compile_options(crtsys_wfp_inspection_contracts
  PRIVATE
    "$<$<COMPILE_LANGUAGE:CXX>:/W4>"
    "$<$<COMPILE_LANGUAGE:CXX>:/WX>"
    "$<$<COMPILE_LANGUAGE:CXX>:/EHsc>"
    "$<$<AND:$<COMPILE_LANGUAGE:CXX>,$<CONFIG:Debug>>:/MTd>"
    "$<$<AND:$<COMPILE_LANGUAGE:CXX>,$<CONFIG:Release>>:/MT>"
)

target_compile_options(crtsys_wfp_protocol_contracts
  PRIVATE
    "$<$<COMPILE_LANGUAGE:CXX>:/W4>"
    "$<$<COMPILE_LANGUAGE:CXX>:/WX>"
    "$<$<COMPILE_LANGUAGE:CXX>:/EHsc>"
    "$<$<AND:$<COMPILE_LANGUAGE:CXX>,$<CONFIG:Debug>>:/MTd>"
    "$<$<AND:$<COMPILE_LANGUAGE:CXX>,$<CONFIG:Release>>:/MT>"
)

target_compile_options(crtsys_wfp_http_transform_contracts
  PRIVATE
    "$<$<COMPILE_LANGUAGE:CXX>:/W4>"
    "$<$<COMPILE_LANGUAGE:CXX>:/WX>"
    "$<$<COMPILE_LANGUAGE:CXX>:/EHsc>"
    "$<$<AND:$<COMPILE_LANGUAGE:CXX>,$<CONFIG:Debug>>:/MTd>"
    "$<$<AND:$<COMPILE_LANGUAGE:CXX>,$<CONFIG:Release>>:/MT>"
)

target_compile_options(crtsys_wfp_fuzz_contracts
  PRIVATE
    "$<$<COMPILE_LANGUAGE:CXX>:/W4>"
    "$<$<COMPILE_LANGUAGE:CXX>:/WX>"
    "$<$<COMPILE_LANGUAGE:CXX>:/EHsc>"
    "$<$<AND:$<COMPILE_LANGUAGE:CXX>,$<CONFIG:Debug>>:/MTd>"
    "$<$<AND:$<COMPILE_LANGUAGE:CXX>,$<CONFIG:Release>>:/MT>"
)

target_compile_options(crtsys_wfp_policy_stress_contracts
  PRIVATE
    "$<$<COMPILE_LANGUAGE:CXX>:/W4>"
    "$<$<COMPILE_LANGUAGE:CXX>:/WX>"
    "$<$<COMPILE_LANGUAGE:CXX>:/EHsc>"
    "$<$<AND:$<COMPILE_LANGUAGE:CXX>,$<CONFIG:Debug>>:/MTd>"
    "$<$<AND:$<COMPILE_LANGUAGE:CXX>,$<CONFIG:Release>>:/MT>"
)

target_compile_options(crtsys_wfp_browser_http3_contracts
  PRIVATE
    "$<$<COMPILE_LANGUAGE:CXX>:/W4>"
    "$<$<COMPILE_LANGUAGE:CXX>:/WX>"
    "$<$<COMPILE_LANGUAGE:CXX>:/EHsc>"
    "$<$<AND:$<COMPILE_LANGUAGE:CXX>,$<CONFIG:Debug>>:/MTd>"
    "$<$<AND:$<COMPILE_LANGUAGE:CXX>,$<CONFIG:Release>>:/MT>"
)

include("${crtsys_SOURCE_DIR}/cmake/std-cxx.cmake")
set_std_cxx_latest(crtsys_wfp_compile_contracts)
set_std_cxx_latest(crtsys_wfp_management_contracts)
set_std_cxx_latest(crtsys_wfp_scatter_contracts)
set_std_cxx_latest(crtsys_wfp_async_socket_contracts)
set_std_cxx_latest(crtsys_wfp_tls_stream_contracts)
set_std_cxx_latest(crtsys_wfp_inspection_contracts)
set_std_cxx_latest(crtsys_wfp_protocol_contracts)
set_std_cxx_latest(crtsys_wfp_http_transform_contracts)
set_std_cxx_latest(crtsys_wfp_fuzz_contracts)
set_std_cxx_latest(crtsys_wfp_policy_stress_contracts)
set_std_cxx_latest(crtsys_wfp_browser_http3_contracts)
if(CRTSYS_WFP_ENABLE_LIBFUZZER)
  set_std_cxx_latest(crtsys_wfp_protocol_libfuzzer)
endif()
