include_guard(GLOBAL)

include("${CMAKE_CURRENT_LIST_DIR}/CPM.cmake")

set(ZLIB_BUILD_TESTING OFF CACHE BOOL "" FORCE)
set(ZLIB_BUILD_SHARED OFF CACHE BOOL "" FORCE)
set(ZLIB_BUILD_STATIC ON CACHE BOOL "" FORCE)
set(ZLIB_INSTALL OFF CACHE BOOL "" FORCE)
CPMAddPackage(
  NAME zlib
  GITHUB_REPOSITORY madler/zlib
  GIT_TAG v1.3.2
)

set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
set(BROTLI_BUILD_TOOLS OFF CACHE BOOL "" FORCE)
set(BROTLI_DISABLE_TESTS ON CACHE BOOL "" FORCE)
set(BROTLI_BUNDLED_MODE ON CACHE BOOL "" FORCE)
CPMAddPackage(
  NAME brotli
  GITHUB_REPOSITORY google/brotli
  GIT_TAG v1.2.0
)

if(MSVC)
  foreach(codec_target
      zlibstatic
      brotlicommon
      brotlidec
      brotlienc)
    if(TARGET ${codec_target})
      set_property(
        TARGET ${codec_target}
        PROPERTY MSVC_RUNTIME_LIBRARY
          "MultiThreaded$<$<CONFIG:Debug>:Debug>")
      target_compile_options(
        ${codec_target}
        PRIVATE
          "$<$<CONFIG:Debug>:/MTd>"
          "$<$<NOT:$<CONFIG:Debug>>:/MT>")
    endif()
  endforeach()
endif()

if(NOT TARGET crtsys_ntl_content_codecs)
  add_library(crtsys_ntl_content_codecs INTERFACE)
  target_link_libraries(
    crtsys_ntl_content_codecs
    INTERFACE
      ZLIB::ZLIBSTATIC
      brotlidec
      brotlicommon
  )
endif()
