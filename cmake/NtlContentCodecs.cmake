include_guard(GLOBAL)

set(_CRTSYS_NTL_PREBUILT_CODEC_ROOT "")
if(DEFINED _CRTSYS_ROOT AND CRTSYS_USE_PREBUILT AND
   COMMAND crtsys_get_prebuilt_arch AND
   COMMAND crtsys_get_prebuilt_toolset AND
   EXISTS "${_CRTSYS_ROOT}/build/native/codecs")
  set(_CRTSYS_NTL_PREBUILT_CODEC_ROOT
    "${_CRTSYS_ROOT}/build/native")
endif()

if(_CRTSYS_NTL_PREBUILT_CODEC_ROOT)
  crtsys_get_prebuilt_arch(_crtsys_ntl_codec_arch)
  crtsys_get_prebuilt_toolset(_crtsys_ntl_codec_toolset)

  function(_crtsys_add_prebuilt_codec_target target codec_group library)
    if(TARGET ${target})
      return()
    endif()

    set(_codec_root
      "${_CRTSYS_NTL_PREBUILT_CODEC_ROOT}/${codec_group}")
    set(_debug_library
      "${_codec_root}/lib/${_crtsys_ntl_codec_toolset}/${_crtsys_ntl_codec_arch}/Debug/${library}")
    set(_release_library
      "${_codec_root}/lib/${_crtsys_ntl_codec_toolset}/${_crtsys_ntl_codec_arch}/Release/${library}")
    set(_has_debug FALSE)
    set(_has_release FALSE)
    if(EXISTS "${_debug_library}")
      set(_has_debug TRUE)
    endif()
    if(EXISTS "${_release_library}")
      set(_has_release TRUE)
    endif()
    if(NOT _has_debug AND NOT _has_release)
      message(FATAL_ERROR
        "No prebuilt NTL codec library was found under ${_codec_root}/lib/${_crtsys_ntl_codec_toolset}/${_crtsys_ntl_codec_arch} for ${library}")
    endif()

    add_library(${target} STATIC IMPORTED GLOBAL)
    if(_has_debug AND _has_release)
      set_target_properties(${target} PROPERTIES
        IMPORTED_CONFIGURATIONS "Debug;Release"
        IMPORTED_LOCATION_DEBUG "${_debug_library}"
        IMPORTED_LOCATION_RELEASE "${_release_library}"
        MAP_IMPORTED_CONFIG_MINSIZEREL Release
        MAP_IMPORTED_CONFIG_RELWITHDEBINFO Release)
    elseif(_has_debug)
      # CI matrix jobs prepare only the configuration they build. A generic
      # imported location keeps a one-configuration bundle usable with a
      # Visual Studio multi-config generator, matching crtsys.lib behavior.
      set_target_properties(${target} PROPERTIES
        IMPORTED_LOCATION "${_debug_library}")
    else()
      set_target_properties(${target} PROPERTIES
        IMPORTED_LOCATION "${_release_library}")
    endif()
  endfunction()

  _crtsys_add_prebuilt_codec_target(
    crtsys_ntl_prebuilt_zlib codecs zlibstatic.lib)
  _crtsys_add_prebuilt_codec_target(
    crtsys_ntl_prebuilt_brotli_common codecs brotlicommon.lib)
  _crtsys_add_prebuilt_codec_target(
    crtsys_ntl_prebuilt_brotli_decoder codecs brotlidec.lib)
  _crtsys_add_prebuilt_codec_target(
    crtsys_ntl_prebuilt_brotli_encoder codecs brotlienc.lib)

  # Preserve the source-build target names for consumers that link a codec
  # directly in addition to using crtsys_ntl_content_codecs. Without these
  # aliases CMake treats names such as brotlienc as bare library filenames.
  if(NOT TARGET zlibstatic)
    add_library(zlibstatic ALIAS crtsys_ntl_prebuilt_zlib)
  endif()
  if(NOT TARGET ZLIB::ZLIBSTATIC)
    add_library(ZLIB::ZLIBSTATIC ALIAS crtsys_ntl_prebuilt_zlib)
  endif()
  if(NOT TARGET brotlicommon)
    add_library(brotlicommon ALIAS crtsys_ntl_prebuilt_brotli_common)
  endif()
  if(NOT TARGET brotlidec)
    add_library(brotlidec ALIAS crtsys_ntl_prebuilt_brotli_decoder)
  endif()
  if(NOT TARGET brotlienc)
    add_library(brotlienc ALIAS crtsys_ntl_prebuilt_brotli_encoder)
  endif()

  add_library(crtsys_ntl_content_codecs INTERFACE)
  target_include_directories(crtsys_ntl_content_codecs SYSTEM INTERFACE
    "${_CRTSYS_NTL_PREBUILT_CODEC_ROOT}/codecs/include")
  target_link_libraries(crtsys_ntl_content_codecs INTERFACE
    crtsys_ntl_prebuilt_zlib
    crtsys_ntl_prebuilt_brotli_decoder
    crtsys_ntl_prebuilt_brotli_encoder
    crtsys_ntl_prebuilt_brotli_common)
  if(MSVC)
    target_compile_options(crtsys_ntl_content_codecs INTERFACE
      "$<$<AND:$<COMPILE_LANGUAGE:C,CXX>,$<CONFIG:Debug>>:/MTd>"
      "$<$<AND:$<COMPILE_LANGUAGE:C,CXX>,$<NOT:$<CONFIG:Debug>>>:/MT>")
  endif()
else()
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
    if(MSVC)
      target_compile_options(
        crtsys_ntl_content_codecs
        INTERFACE
          "$<$<AND:$<COMPILE_LANGUAGE:C,CXX>,$<CONFIG:Debug>>:/MTd>"
          "$<$<AND:$<COMPILE_LANGUAGE:C,CXX>,$<NOT:$<CONFIG:Debug>>>:/MT>"
      )
    endif()
    target_link_libraries(
      crtsys_ntl_content_codecs
      INTERFACE
        ZLIB::ZLIBSTATIC
        brotlidec
        brotlienc
        brotlicommon
    )
  endif()
endif()

# Adds audited WDK static libraries for PASSIVE_LEVEL kernel content
# transformation. This is a function so user-mode consumers do not build a
# second copy of the codec sources merely because FindWDK is available.
function(crtsys_add_ntl_kernel_content_codecs)
  if(TARGET crtsys_ntl_kernel_content_codecs)
    return()
  endif()

  if(_CRTSYS_NTL_PREBUILT_CODEC_ROOT)
    _crtsys_add_prebuilt_codec_target(
      crtsys_ntl_prebuilt_kernel_zlib
      kernel-codecs crtsys_ntl_kernel_zlib.lib)
    _crtsys_add_prebuilt_codec_target(
      crtsys_ntl_prebuilt_kernel_brotli
      kernel-codecs crtsys_ntl_kernel_brotli.lib)
    add_library(crtsys_ntl_kernel_content_codecs INTERFACE)
    target_link_libraries(crtsys_ntl_kernel_content_codecs INTERFACE
      crtsys_ntl_prebuilt_kernel_zlib
      crtsys_ntl_prebuilt_kernel_brotli)
    target_compile_definitions(crtsys_ntl_kernel_content_codecs
      INTERFACE Z_SOLO)
    target_include_directories(crtsys_ntl_kernel_content_codecs
      SYSTEM INTERFACE
        "${_CRTSYS_NTL_PREBUILT_CODEC_ROOT}/codecs/include")
    return()
  endif()

  if(NOT COMMAND wdk_add_library)
    message(FATAL_ERROR
      "Kernel content codecs require FindWDK and wdk_add_library")
  endif()
  if(NOT COMMAND crtsys_apply_driver_settings)
    message(FATAL_ERROR
      "Include cmake/CrtSys.cmake before adding kernel content codecs")
  endif()

  set(_ntl_kernel_zlib_sources
    "${zlib_SOURCE_DIR}/adler32.c"
    "${zlib_SOURCE_DIR}/crc32.c"
    "${zlib_SOURCE_DIR}/deflate.c"
    "${zlib_SOURCE_DIR}/infback.c"
    "${zlib_SOURCE_DIR}/inffast.c"
    "${zlib_SOURCE_DIR}/inflate.c"
    "${zlib_SOURCE_DIR}/inftrees.c"
    "${zlib_SOURCE_DIR}/trees.c"
    "${zlib_SOURCE_DIR}/zutil.c"
  )
  file(GLOB _ntl_kernel_brotli_sources CONFIGURE_DEPENDS
    "${brotli_SOURCE_DIR}/c/common/*.c"
    "${brotli_SOURCE_DIR}/c/dec/*.c"
    "${brotli_SOURCE_DIR}/c/enc/*.c"
  )

  wdk_add_library(crtsys_ntl_kernel_zlib STATIC
    ${_ntl_kernel_zlib_sources})
  wdk_add_library(crtsys_ntl_kernel_brotli STATIC
    ${_ntl_kernel_brotli_sources})

  crtsys_apply_driver_settings(
    crtsys_ntl_kernel_zlib "${_CRTSYS_ROOT}"
    FALSE FALSE FALSE)
  crtsys_apply_driver_settings(
    crtsys_ntl_kernel_brotli "${_CRTSYS_ROOT}"
    FALSE FALSE FALSE)
  crtsys_scope_compile_options_to_c_cxx(crtsys_ntl_kernel_zlib)
  crtsys_scope_compile_options_to_c_cxx(crtsys_ntl_kernel_brotli)
  target_compile_options(crtsys_ntl_kernel_zlib
    PRIVATE "$<$<COMPILE_LANGUAGE:C,CXX>:/MT>")
  target_compile_options(crtsys_ntl_kernel_brotli
    PRIVATE "$<$<COMPILE_LANGUAGE:C,CXX>:/MT>")
  target_compile_definitions(crtsys_ntl_kernel_zlib
    PRIVATE _KERNEL32_ _ITERATOR_DEBUG_LEVEL=0)
  target_compile_definitions(crtsys_ntl_kernel_brotli
    PRIVATE _KERNEL32_ _ITERATOR_DEBUG_LEVEL=0)

  target_include_directories(crtsys_ntl_kernel_zlib
    PUBLIC "${zlib_SOURCE_DIR}")
  target_compile_definitions(crtsys_ntl_kernel_zlib
    PUBLIC Z_SOLO)

  target_include_directories(crtsys_ntl_kernel_brotli
    PUBLIC "${brotli_SOURCE_DIR}/c/include")
  target_compile_definitions(crtsys_ntl_kernel_brotli
    PRIVATE _CRT_SECURE_NO_WARNINGS)
  if(MSVC)
    # The pinned upstream C implementation intentionally leaves parameters
    # and generated matcher locals unused in selected configurations and uses
    # anonymous unions. Keep /WX for the target while suppressing only those
    # third-party diagnostics.
    target_compile_options(crtsys_ntl_kernel_brotli
      PRIVATE /wd4100 /wd4127 /wd4189 /wd4201)
  endif()

  add_library(crtsys_ntl_kernel_content_codecs INTERFACE)
  target_link_libraries(crtsys_ntl_kernel_content_codecs
    INTERFACE
      crtsys_ntl_kernel_zlib
      crtsys_ntl_kernel_brotli)
  target_compile_definitions(crtsys_ntl_kernel_content_codecs
    INTERFACE Z_SOLO)
  target_include_directories(crtsys_ntl_kernel_content_codecs
    INTERFACE
      "${zlib_SOURCE_DIR}"
      "${brotli_SOURCE_DIR}/c/include")
endfunction()
