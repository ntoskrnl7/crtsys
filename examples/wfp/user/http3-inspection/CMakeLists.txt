cmake_minimum_required(VERSION 3.14 FATAL_ERROR)

project(crtsys_wfp_http3_inspection LANGUAGES C CXX)

include("${CMAKE_CURRENT_LIST_DIR}/../../../../cmake/CPM.cmake")

CPMAddPackage(NAME crtsys SOURCE_DIR "${CMAKE_CURRENT_LIST_DIR}/../../../..")
include("${crtsys_SOURCE_DIR}/cmake/CrtSys.cmake")
include("${crtsys_SOURCE_DIR}/cmake/NtlContentCodecs.cmake")
include("${crtsys_SOURCE_DIR}/cmake/NtlMsQuic.cmake")
crtsys_add_ntl_msquic_headers()

crtsys_add_driver(crtsys_wfp_http3_inspection_driver
  WFP
  NTL
  driver/main.cpp
)

add_executable(crtsys_wfp_http3_inspection_service
  app/main.cpp
  app/http3_service.cpp
  app/wfp_gate.cpp
)
set(crtsys_wfp_user_http3_fixture_dir
    "${crtsys_SOURCE_DIR}/test/wfp/runtime/fixtures/user/http3-inspection")
add_executable(crtsys_wfp_http3_inspection_acceptance
  "${crtsys_wfp_user_http3_fixture_dir}/acceptance_main.cpp"
  "${crtsys_wfp_user_http3_fixture_dir}/controlled_msquic.cpp"
)
add_executable(crtsys_wfp_http3_replay_contracts
  "${crtsys_wfp_user_http3_fixture_dir}/replay_contract.cpp"
)

target_include_directories(crtsys_wfp_http3_inspection_service
  PRIVATE
    "${crtsys_SOURCE_DIR}/include"
    "${CMAKE_CURRENT_LIST_DIR}/app"
    "${CMAKE_CURRENT_LIST_DIR}/shared"
    "${CMAKE_CURRENT_LIST_DIR}/../../shared"
)
target_include_directories(crtsys_wfp_http3_inspection_acceptance
  PRIVATE
    "${crtsys_SOURCE_DIR}/include"
    "${crtsys_wfp_user_http3_fixture_dir}"
    "${crtsys_SOURCE_DIR}/test/wfp/runtime/fixtures"
    "${CMAKE_CURRENT_LIST_DIR}/shared"
    "${CMAKE_CURRENT_LIST_DIR}/../../shared"
)
target_include_directories(crtsys_wfp_http3_inspection_driver
  PRIVATE "${CMAKE_CURRENT_LIST_DIR}/shared"
)
target_include_directories(crtsys_wfp_http3_replay_contracts
  PRIVATE
    "${crtsys_SOURCE_DIR}/include"
    "${CMAKE_CURRENT_LIST_DIR}/../../shared"
)
target_link_libraries(crtsys_wfp_http3_inspection_service
  PRIVATE
    crtsys_ntl_msquic_headers
    crtsys_ntl_content_codecs
    brotlienc
    advapi32
    crypt32
    fwpuclnt
    uuid
    ws2_32
)
target_link_libraries(crtsys_wfp_http3_inspection_acceptance
  PRIVATE
    crtsys_ntl_msquic_headers
    crtsys_ntl_content_codecs
    brotlienc
    advapi32
    crypt32
    ws2_32
)
target_link_libraries(crtsys_wfp_http3_inspection_driver WDK::NDIS)
target_link_libraries(crtsys_wfp_http3_replay_contracts
  PRIVATE
    crtsys_ntl_content_codecs
    brotlienc
)
target_compile_definitions(crtsys_wfp_http3_inspection_service
  PRIVATE QUIC_API_ENABLE_PREVIEW_FEATURES)
target_compile_definitions(crtsys_wfp_http3_inspection_acceptance
  PRIVATE QUIC_API_ENABLE_PREVIEW_FEATURES)
target_compile_options(crtsys_wfp_http3_inspection_service
  PRIVATE
    "$<$<COMPILE_LANGUAGE:CXX>:/W4>"
    "$<$<COMPILE_LANGUAGE:CXX>:/WX>"
    "$<$<COMPILE_LANGUAGE:CXX>:/EHsc>"
    "$<$<AND:$<COMPILE_LANGUAGE:CXX>,$<CONFIG:Debug>>:/MTd>"
    "$<$<AND:$<COMPILE_LANGUAGE:CXX>,$<CONFIG:Release>>:/MT>"
)
target_compile_options(crtsys_wfp_http3_inspection_acceptance
  PRIVATE
    "$<$<COMPILE_LANGUAGE:CXX>:/W4>"
    "$<$<COMPILE_LANGUAGE:CXX>:/WX>"
    "$<$<COMPILE_LANGUAGE:CXX>:/EHsc>"
    "$<$<AND:$<COMPILE_LANGUAGE:CXX>,$<CONFIG:Debug>>:/MTd>"
    "$<$<AND:$<COMPILE_LANGUAGE:CXX>,$<CONFIG:Release>>:/MT>"
)
target_compile_options(crtsys_wfp_http3_inspection_driver
  PRIVATE
    "$<$<COMPILE_LANGUAGE:CXX>:/W4>"
    "$<$<COMPILE_LANGUAGE:CXX>:/WX>"
)
target_compile_options(crtsys_wfp_http3_replay_contracts
  PRIVATE
    "$<$<COMPILE_LANGUAGE:CXX>:/W4>"
    "$<$<COMPILE_LANGUAGE:CXX>:/WX>"
    "$<$<COMPILE_LANGUAGE:CXX>:/EHsc>"
    "$<$<AND:$<COMPILE_LANGUAGE:CXX>,$<CONFIG:Debug>>:/MTd>"
    "$<$<AND:$<COMPILE_LANGUAGE:CXX>,$<CONFIG:Release>>:/MT>"
)

if(CMAKE_GENERATOR_PLATFORM MATCHES "^ARM64")
  set(crtsys_wfp_http3_architecture _ARM64_)
elseif(CMAKE_GENERATOR_PLATFORM MATCHES "^ARM")
  set(crtsys_wfp_http3_architecture _ARM_)
elseif(CMAKE_SIZEOF_VOID_P EQUAL 8)
  set(crtsys_wfp_http3_architecture _AMD64_)
else()
  set(crtsys_wfp_http3_architecture _X86_)
endif()
target_compile_definitions(
  crtsys_wfp_http3_inspection_service
  PRIVATE ${crtsys_wfp_http3_architecture})
target_compile_definitions(
  crtsys_wfp_http3_inspection_acceptance
  PRIVATE ${crtsys_wfp_http3_architecture})
target_compile_definitions(
  crtsys_wfp_http3_replay_contracts
  PRIVATE ${crtsys_wfp_http3_architecture})

add_custom_command(TARGET crtsys_wfp_http3_inspection_driver POST_BUILD
  COMMAND "${CMAKE_COMMAND}" -E copy_if_different
          "${CMAKE_CURRENT_LIST_DIR}/crtsys_wfp_http3_inspection_driver.inf"
          "$<TARGET_FILE_DIR:crtsys_wfp_http3_inspection_driver>"
)

include("${crtsys_SOURCE_DIR}/cmake/std-cxx.cmake")
set_std_cxx_latest(crtsys_wfp_http3_inspection_service)
set_std_cxx_latest(crtsys_wfp_http3_inspection_acceptance)
set_std_cxx_latest(crtsys_wfp_http3_inspection_driver)
set_std_cxx_latest(crtsys_wfp_http3_replay_contracts)

include(CTest)
if(BUILD_TESTING)
  add_test(NAME crtsys_wfp_http3_replay_contracts
           COMMAND crtsys_wfp_http3_replay_contracts)
endif()
