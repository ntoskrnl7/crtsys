cmake_minimum_required(VERSION 3.14 FATAL_ERROR)

project(crtsys_wfp_compile_contracts LANGUAGES C CXX)

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

add_executable(crtsys_wfp_browser_http3_contracts
  browser_http3.cpp
  ../../../examples/wfp/browser-https-inspection/app/browser_log.cpp
  ../../../examples/wfp/browser-https-inspection/app/http3_inspection.cpp
)

target_link_libraries(crtsys_wfp_protocol_contracts
  PRIVATE
    crtsys_ntl_content_codecs
    brotlienc
)

target_link_libraries(crtsys_wfp_browser_http3_contracts
  PRIVATE
    crtsys_ntl_content_codecs
    brotlienc
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
  crtsys_wfp_browser_http3_contracts
  PRIVATE ${crtsys_wfp_contract_architecture})

target_link_libraries(crtsys_wfp_management_contracts
  PRIVATE
    fwpuclnt
)

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
set_std_cxx_latest(crtsys_wfp_browser_http3_contracts)
