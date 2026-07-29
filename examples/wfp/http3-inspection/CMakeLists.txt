cmake_minimum_required(VERSION 3.14 FATAL_ERROR)

project(crtsys_wfp_http3_inspection LANGUAGES C CXX)

include("${CMAKE_CURRENT_LIST_DIR}/../../../cmake/CPM.cmake")

CPMAddPackage(NAME crtsys SOURCE_DIR "${CMAKE_CURRENT_LIST_DIR}/../../..")
include("${crtsys_SOURCE_DIR}/cmake/NtlContentCodecs.cmake")

add_executable(crtsys_wfp_http3_inspection
  app/main.cpp
)

target_include_directories(crtsys_wfp_http3_inspection
  PRIVATE "${crtsys_SOURCE_DIR}/include"
)
target_link_libraries(crtsys_wfp_http3_inspection
  PRIVATE
    crtsys_ntl_content_codecs
    brotlienc
)
target_compile_options(crtsys_wfp_http3_inspection
  PRIVATE
    "$<$<COMPILE_LANGUAGE:CXX>:/W4>"
    "$<$<COMPILE_LANGUAGE:CXX>:/WX>"
    "$<$<COMPILE_LANGUAGE:CXX>:/EHsc>"
    "$<$<AND:$<COMPILE_LANGUAGE:CXX>,$<CONFIG:Debug>>:/MTd>"
    "$<$<AND:$<COMPILE_LANGUAGE:CXX>,$<CONFIG:Release>>:/MT>"
)

if(CMAKE_SIZEOF_VOID_P EQUAL 8)
  target_compile_definitions(
    crtsys_wfp_http3_inspection PRIVATE _AMD64_)
else()
  target_compile_definitions(
    crtsys_wfp_http3_inspection PRIVATE _X86_)
endif()

include("${crtsys_SOURCE_DIR}/cmake/std-cxx.cmake")
set_std_cxx_latest(crtsys_wfp_http3_inspection)
