cmake_minimum_required(VERSION 3.20 FATAL_ERROR)

project(crtsys_nuget_content_codecs LANGUAGES C)

set(CRTSYS_ROOT "" CACHE PATH "crtsys source root")
if(NOT CRTSYS_ROOT)
  message(FATAL_ERROR "Set CRTSYS_ROOT to the crtsys source root.")
endif()

include("${CRTSYS_ROOT}/cmake/NtlContentCodecs.cmake")

add_custom_target(
  crtsys_nuget_content_codecs ALL
  DEPENDS zlibstatic brotlicommon brotlidec brotlienc
)

install(
  FILES "$<TARGET_FILE:zlibstatic>"
  DESTINATION lib
  RENAME zlibstatic.lib
  COMPONENT CrtSysContentCodecs
)
install(
  FILES
    "$<TARGET_FILE:brotlicommon>"
    "$<TARGET_FILE:brotlidec>"
    "$<TARGET_FILE:brotlienc>"
  DESTINATION lib
  COMPONENT CrtSysContentCodecs
)
install(
  FILES
    "${zlib_SOURCE_DIR}/zlib.h"
    "${zlib_BINARY_DIR}/zconf.h"
  DESTINATION include
  COMPONENT CrtSysContentCodecs
)
install(
  DIRECTORY "${brotli_SOURCE_DIR}/c/include/brotli"
  DESTINATION include
  COMPONENT CrtSysContentCodecs
)
