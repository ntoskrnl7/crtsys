cmake_minimum_required(VERSION 3.14 FATAL_ERROR)

project(crtsys_wfp_user_http3_inspection_acceptance LANGUAGES C CXX)

include("${CMAKE_CURRENT_LIST_DIR}/../../../../../../cmake/CPM.cmake")
CPMAddPackage(
  NAME crtsys
  SOURCE_DIR "${CMAKE_CURRENT_LIST_DIR}/../../../../../.."
)
include("${crtsys_SOURCE_DIR}/cmake/NtlContentCodecs.cmake")
include("${crtsys_SOURCE_DIR}/cmake/NtlMsQuic.cmake")
crtsys_add_ntl_msquic_headers()

add_executable(crtsys_wfp_http3_inspection_acceptance
  acceptance_main.cpp
  controlled_msquic.cpp
)
add_executable(crtsys_wfp_http3_replay_contracts
  replay_contract.cpp
)

target_include_directories(crtsys_wfp_http3_inspection_acceptance
  PRIVATE
    "${crtsys_SOURCE_DIR}/include"
    "${CMAKE_CURRENT_LIST_DIR}"
    "${CMAKE_CURRENT_LIST_DIR}/../.."
    "${crtsys_SOURCE_DIR}/examples/wfp/user/http3-inspection/shared"
    "${crtsys_SOURCE_DIR}/examples/wfp/shared"
)
target_include_directories(crtsys_wfp_http3_replay_contracts
  PRIVATE
    "${crtsys_SOURCE_DIR}/include"
    "${crtsys_SOURCE_DIR}/examples/wfp/shared"
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
target_link_libraries(crtsys_wfp_http3_replay_contracts
  PRIVATE
    crtsys_ntl_content_codecs
    brotlienc
)
target_compile_definitions(crtsys_wfp_http3_inspection_acceptance
  PRIVATE QUIC_API_ENABLE_PREVIEW_FEATURES)

if(CMAKE_GENERATOR_PLATFORM MATCHES "^ARM64")
  set(crtsys_wfp_http3_architecture _ARM64_)
elseif(CMAKE_GENERATOR_PLATFORM MATCHES "^ARM")
  set(crtsys_wfp_http3_architecture _ARM_)
elseif(CMAKE_SIZEOF_VOID_P EQUAL 8)
  set(crtsys_wfp_http3_architecture _AMD64_)
else()
  set(crtsys_wfp_http3_architecture _X86_)
endif()
target_compile_definitions(crtsys_wfp_http3_inspection_acceptance
  PRIVATE ${crtsys_wfp_http3_architecture})
target_compile_definitions(crtsys_wfp_http3_replay_contracts
  PRIVATE ${crtsys_wfp_http3_architecture})

foreach(target IN ITEMS
    crtsys_wfp_http3_inspection_acceptance
    crtsys_wfp_http3_replay_contracts)
  target_compile_options(${target} PRIVATE
    "$<$<COMPILE_LANGUAGE:CXX>:/W4>"
    "$<$<COMPILE_LANGUAGE:CXX>:/WX>"
    "$<$<COMPILE_LANGUAGE:CXX>:/EHsc>"
    "$<$<AND:$<COMPILE_LANGUAGE:CXX>,$<CONFIG:Debug>>:/MTd>"
    "$<$<AND:$<COMPILE_LANGUAGE:CXX>,$<CONFIG:Release>>:/MT>"
  )
  set_property(TARGET ${target} PROPERTY CXX_STANDARD 23)
  set_property(TARGET ${target} PROPERTY CXX_STANDARD_REQUIRED YES)
endforeach()

include(CTest)
if(BUILD_TESTING)
  add_test(NAME crtsys_wfp_http3_replay_contracts
           COMMAND crtsys_wfp_http3_replay_contracts)
endif()
