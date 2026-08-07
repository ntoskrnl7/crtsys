cmake_minimum_required(VERSION 3.14 FATAL_ERROR)

project(crtsys_wfp_kernel_http3_inspection_acceptance LANGUAGES CXX)

include("${CMAKE_CURRENT_LIST_DIR}/../../../../../../cmake/CPM.cmake")
CPMAddPackage(
  NAME crtsys
  SOURCE_DIR "${CMAKE_CURRENT_LIST_DIR}/../../../../../.."
)
include("${crtsys_SOURCE_DIR}/cmake/NtlContentCodecs.cmake")
include("${crtsys_SOURCE_DIR}/cmake/NtlMsQuic.cmake")
crtsys_add_ntl_msquic_headers()

add_executable(crtsys_wfp_kernel_http3_inspection_acceptance
  main.cpp
  msquic_client.cpp
)
target_include_directories(crtsys_wfp_kernel_http3_inspection_acceptance
  PRIVATE
    "${crtsys_SOURCE_DIR}/include"
    "${CMAKE_CURRENT_LIST_DIR}"
    "${CMAKE_CURRENT_LIST_DIR}/../.."
)
target_compile_definitions(crtsys_wfp_kernel_http3_inspection_acceptance
  PRIVATE QUIC_API_ENABLE_PREVIEW_FEATURES)
target_link_libraries(crtsys_wfp_kernel_http3_inspection_acceptance
  PRIVATE
    crtsys_ntl_msquic_headers
    crtsys_ntl_content_codecs
    ws2_32
)
target_compile_options(crtsys_wfp_kernel_http3_inspection_acceptance PRIVATE
  /W4 /WX /EHsc
  "$<$<CONFIG:Debug>:/MTd>"
  "$<$<CONFIG:Release>:/MT>"
)
set_property(TARGET crtsys_wfp_kernel_http3_inspection_acceptance
  PROPERTY CXX_STANDARD 23)
set_property(TARGET crtsys_wfp_kernel_http3_inspection_acceptance
  PROPERTY CXX_STANDARD_REQUIRED YES)
