cmake_minimum_required(VERSION 3.14 FATAL_ERROR)

project(crtsys_wfp_compile_contracts LANGUAGES C CXX)

enable_testing()

find_program(CRTSYS_POWERSHELL_EXECUTABLE NAMES pwsh powershell REQUIRED)
add_test(
  NAME crtsys_wfp_example_pair_parity
  COMMAND "${CRTSYS_POWERSHELL_EXECUTABLE}"
          -NoProfile
          -ExecutionPolicy Bypass
          -File "${CMAKE_CURRENT_LIST_DIR}/../Test-WfpExamplePairParity.ps1"
          -RepositoryRoot "${CMAKE_CURRENT_LIST_DIR}/../../.."
)
add_test(
  NAME crtsys_wfp_inf_packages
  COMMAND "${CRTSYS_POWERSHELL_EXECUTABLE}"
          -NoProfile
          -ExecutionPolicy Bypass
          -File "${CMAKE_CURRENT_LIST_DIR}/../Test-WfpInfPackages.ps1"
          -RepositoryRoot "${CMAKE_CURRENT_LIST_DIR}/../../.."
)
add_test(
  NAME crtsys_wfp_disposable_guest_guard
  COMMAND "${CRTSYS_POWERSHELL_EXECUTABLE}"
          -NoProfile
          -ExecutionPolicy Bypass
          -File
          "${CMAKE_CURRENT_LIST_DIR}/../runtime/common/Test-DisposableGuestGuardContract.ps1"
)
add_test(
  NAME crtsys_wfp_runtime_packaging
  COMMAND "${CRTSYS_POWERSHELL_EXECUTABLE}"
          -NoProfile
          -ExecutionPolicy Bypass
          -File
          "${CMAKE_CURRENT_LIST_DIR}/../runtime/advanced/Test-WfpRuntimePackagingContract.ps1"
)
add_test(
  NAME crtsys_ntl_net_kernel_runtime_packaging
  COMMAND "${CRTSYS_POWERSHELL_EXECUTABLE}"
          -NoProfile
          -ExecutionPolicy Bypass
          -File
          "${CMAKE_CURRENT_LIST_DIR}/../../net/kernel-contracts/Test-NtlNetKernelRuntimePackagingContract.ps1"
          -RepositoryRoot "${CMAKE_CURRENT_LIST_DIR}/../../.."
)
option(CRTSYS_WFP_ENABLE_LIBFUZZER
  "Build the Clang coverage-guided WFP protocol fuzz target" OFF)

include("${CMAKE_CURRENT_LIST_DIR}/../../../cmake/CPM.cmake")

CPMAddPackage(NAME crtsys SOURCE_DIR "${CMAKE_CURRENT_LIST_DIR}/../../..")
include("${crtsys_SOURCE_DIR}/cmake/CrtSys.cmake")

crtsys_add_driver(crtsys_wfp_compile_contracts
  WFP
  NTL
  KERNEL_CONTENT_CODECS
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

add_executable(crtsys_ntl_transform_pipeline_contracts
  transform_pipeline.cpp
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
    PRIVATE -O1 -gline-tables-only /MT)
  if(CMAKE_GENERATOR MATCHES "^Visual Studio")
    # Visual Studio generators invoke lld-link directly.  Passing the Clang
    # driver option through target_link_options() therefore drops the
    # compiler-rt runtimes.  Use the ClangCL MSBuild properties so both
    # instrumentation and runtime linkage are configured by the toolset.
    set_property(TARGET crtsys_wfp_protocol_libfuzzer PROPERTY
      VS_GLOBAL_EnableASAN "true")
    set_property(TARGET crtsys_wfp_protocol_libfuzzer PROPERTY
      VS_GLOBAL_EnableFuzzer "true")
    if(CMAKE_VS_PLATFORM_NAME STREQUAL "x64")
      set(crtsys_wfp_fuzzer_runtime_arch "x86_64")
    elseif(CMAKE_VS_PLATFORM_NAME STREQUAL "Win32")
      set(crtsys_wfp_fuzzer_runtime_arch "i386")
    elseif(CMAKE_VS_PLATFORM_NAME STREQUAL "ARM64")
      set(crtsys_wfp_fuzzer_runtime_arch "aarch64")
    else()
      message(FATAL_ERROR
        "Unsupported ClangCL libFuzzer platform: ${CMAKE_VS_PLATFORM_NAME}")
    endif()
    execute_process(
      COMMAND "${CMAKE_CXX_COMPILER}" -print-resource-dir
      RESULT_VARIABLE crtsys_wfp_clang_resource_result
      OUTPUT_VARIABLE crtsys_wfp_clang_resource_dir
      OUTPUT_STRIP_TRAILING_WHITESPACE)
    if(NOT crtsys_wfp_clang_resource_result EQUAL 0 OR
       crtsys_wfp_clang_resource_dir STREQUAL "")
      message(FATAL_ERROR "ClangCL did not report its resource directory")
    endif()
    set(crtsys_wfp_fuzzer_runtime
      "${crtsys_wfp_clang_resource_dir}/lib/windows/clang_rt.fuzzer-${crtsys_wfp_fuzzer_runtime_arch}.lib")
    if(NOT EXISTS "${crtsys_wfp_fuzzer_runtime}")
      message(FATAL_ERROR
        "The ClangCL libFuzzer runtime was not found: ${crtsys_wfp_fuzzer_runtime}")
    endif()
    target_link_libraries(crtsys_wfp_protocol_libfuzzer
      PRIVATE "${crtsys_wfp_fuzzer_runtime}")
    target_link_options(crtsys_wfp_protocol_libfuzzer
      PRIVATE /INCREMENTAL:NO)
  else()
    target_compile_options(crtsys_wfp_protocol_libfuzzer
      PRIVATE -fsanitize=fuzzer,address)
    target_link_options(crtsys_wfp_protocol_libfuzzer
      PRIVATE -fsanitize=fuzzer,address)
  endif()
  set_property(TARGET crtsys_wfp_protocol_libfuzzer PROPERTY
    MSVC_RUNTIME_LIBRARY "MultiThreaded")
endif()

add_executable(crtsys_wfp_policy_stress_contracts
  policy_stress.cpp
)

add_executable(crtsys_wfp_browser_http3_contracts
  ../../../examples/wfp/user/browser-https-inspection/test/http3_proxy_connection_contracts.cpp
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

target_include_directories(crtsys_ntl_transform_pipeline_contracts
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
    "${crtsys_SOURCE_DIR}/examples/wfp/shared"
    "${crtsys_SOURCE_DIR}/examples/wfp/user/browser-https-inspection/app"
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
  crtsys_ntl_transform_pipeline_contracts
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
    uuid
)

foreach(contract_target
    crtsys_wfp_management_contracts
    crtsys_wfp_scatter_contracts
    crtsys_wfp_async_socket_contracts
    crtsys_wfp_tls_stream_contracts
    crtsys_wfp_inspection_contracts
    crtsys_ntl_transform_pipeline_contracts
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

target_compile_options(crtsys_ntl_transform_pipeline_contracts
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
set_std_cxx_latest(crtsys_ntl_transform_pipeline_contracts)
set_std_cxx_latest(crtsys_wfp_protocol_contracts)
set_std_cxx_latest(crtsys_wfp_http_transform_contracts)
set_std_cxx_latest(crtsys_wfp_fuzz_contracts)
set_std_cxx_latest(crtsys_wfp_policy_stress_contracts)
set_std_cxx_latest(crtsys_wfp_browser_http3_contracts)
if(CRTSYS_WFP_ENABLE_LIBFUZZER)
  set_std_cxx_latest(crtsys_wfp_protocol_libfuzzer)
endif()
