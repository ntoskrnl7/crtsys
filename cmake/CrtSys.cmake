# INCLUDE_DIRECTORIES $(VC_IncludePath);$(WindowsSDK_IncludePath)
cmake_policy(SET CMP0021 OLD)

set(CMAKE_CXX_STANDARD_LIBRARIES " ")
set(CMAKE_C_STANDARD_LIBRARIES ${CMAKE_CXX_STANDARD_LIBRARIES})

if(NOT DEFINED CRTSYS_NTL_MAIN)
    set(CRTSYS_NTL_MAIN ON)
endif()

if(NOT DEFINED CRTSYS_USE_LIBCNTPR)
    set(CRTSYS_USE_LIBCNTPR ON)
endif()

if(DEFINED crtsys_SOURCE_DIR)
    set(_CRTSYS_ROOT "${crtsys_SOURCE_DIR}")
else()
    get_filename_component(_CRTSYS_ROOT "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)
endif()

if(NOT DEFINED CRTSYS_USE_PREBUILT)
    if(NOT TARGET crtsys AND EXISTS "${_CRTSYS_ROOT}/lib/native")
        set(CRTSYS_USE_PREBUILT ON)
    else()
        set(CRTSYS_USE_PREBUILT OFF)
    endif()
endif()

function(crtsys_scope_compile_options_to_c_cxx TARGET_NAME)
    get_target_property(TARGET_COMPILE_OPTIONS ${TARGET_NAME} COMPILE_OPTIONS)
    if(NOT TARGET_COMPILE_OPTIONS)
        return()
    endif()

    set(SCOPED_COMPILE_OPTIONS)
    foreach(COMPILE_OPTION IN LISTS TARGET_COMPILE_OPTIONS)
        list(APPEND SCOPED_COMPILE_OPTIONS "$<$<COMPILE_LANGUAGE:C,CXX>:${COMPILE_OPTION}>")
    endforeach()

    set_target_properties(${TARGET_NAME} PROPERTIES COMPILE_OPTIONS "${SCOPED_COMPILE_OPTIONS}")
endfunction()

# Remove Runtime Checks
string(REGEX REPLACE "/RTC(su|[1su])" "" CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS}")
string(REGEX REPLACE "/RTC(su|[1su])" "" CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG}")
string(REGEX REPLACE "/RTC(su|[1su])" "" CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE}")
string(REGEX REPLACE "/RTC(su|[1su])" "" CMAKE_CXX_FLAGS_MINSIZEREL "${CMAKE_CXX_FLAGS_MINSIZEREL}")
string(REGEX REPLACE "/RTC(su|[1su])" "" CMAKE_CXX_FLAGS_RELWITHDEBINFO "${CMAKE_CXX_FLAGS_RELWITHDEBINFO}")

#---------------------------------------------------------------------------------------------------
# include("${CMAKE_CURRENT_LIST_DIR}/CPM.cmake")

set(CPM_DOWNLOAD_VERSION 0.32.0)

if(CPM_SOURCE_CACHE)
  # Expand relative path. This is important if the provided path contains a tilde (~)
  get_filename_component(CPM_SOURCE_CACHE ${CPM_SOURCE_CACHE} ABSOLUTE)
  set(CPM_DOWNLOAD_LOCATION "${CPM_SOURCE_CACHE}/cpm/CPM_${CPM_DOWNLOAD_VERSION}.cmake")
elseif(DEFINED ENV{CPM_SOURCE_CACHE})
  set(CPM_DOWNLOAD_LOCATION "$ENV{CPM_SOURCE_CACHE}/cpm/CPM_${CPM_DOWNLOAD_VERSION}.cmake")
else()
  set(CPM_DOWNLOAD_LOCATION "${CMAKE_BINARY_DIR}/cmake/CPM_${CPM_DOWNLOAD_VERSION}.cmake")
endif()

if(NOT (EXISTS ${CPM_DOWNLOAD_LOCATION}))
  message(STATUS "Downloading CPM.cmake to ${CPM_DOWNLOAD_LOCATION}")
  file(DOWNLOAD
    https://github.com/cpm-cmake/CPM.cmake/releases/download/v${CPM_DOWNLOAD_VERSION}/CPM.cmake
    ${CPM_DOWNLOAD_LOCATION}
  )
endif()

include(${CPM_DOWNLOAD_LOCATION})
#---------------------------------------------------------------------------------------------------



CPMAddPackage("gh:ntoskrnl7/FindWDK#master")
list(APPEND CMAKE_MODULE_PATH "${FindWDK_SOURCE_DIR}/cmake")
find_package(WDK REQUIRED)

function(crtsys_get_prebuilt_arch _out_arch)
    if("${CMAKE_VS_PLATFORM_NAME}" STREQUAL "x64")
        set(_arch x64)
    elseif("${CMAKE_VS_PLATFORM_NAME}" STREQUAL "ARM64")
        set(_arch ARM64)
    elseif("${CMAKE_VS_PLATFORM_NAME}" STREQUAL "Win32")
        message(FATAL_ERROR "The crtsys prebuilt driver bundle does not include x86 driver libraries.")
    else()
        message(FATAL_ERROR "Unsupported crtsys prebuilt platform: ${CMAKE_VS_PLATFORM_NAME}")
    endif()

    set(${_out_arch} "${_arch}" PARENT_SCOPE)
endfunction()

function(crtsys_get_prebuilt_library _out_path _library _configuration)
    crtsys_get_prebuilt_arch(_arch)

    if("${_configuration}" STREQUAL "Debug")
        set(_config_dir Debug)
    else()
        set(_config_dir Release)
    endif()

    set(_path "${_CRTSYS_ROOT}/lib/native/${_arch}/${_config_dir}/${_library}")
    file(TO_CMAKE_PATH "${_path}" _path)
    if(NOT EXISTS "${_path}")
        message(FATAL_ERROR "Required crtsys prebuilt library was not found: ${_path}")
    endif()

    set(${_out_path} "${_path}" PARENT_SCOPE)
endfunction()

function(crtsys_apply_driver_settings _target _root)
    get_target_property(INC_DIR_TMP ${_target} INCLUDE_DIRECTORIES)
    if(NOT INC_DIR_TMP)
        set(INC_DIR_TMP "")
    endif()

    set_property(TARGET ${_target} PROPERTY INCLUDE_DIRECTORIES "${_root}/include;${_root}/include/.internal/msvc/$(VCToolsVersion);${_root}/include/.internal/msvc/${MSVC_TOOLSET_VERSION};${_root}/include/.internal/msvc/$(VCToolsVersion)/stl;${_root}/include/.internal/msvc/${MSVC_TOOLSET_VERSION}/stl;$(VC_IncludePath);$(WindowsSDK_IncludePath);${INC_DIR_TMP}")

    if(EXISTS "${_root}/include/.internal/winsdk/${CMAKE_VS_WINDOWS_TARGET_PLATFORM_VERSION}/wdk/${WDK_VERSION}/forced.h")
      target_compile_options(${_target} PRIVATE "$<$<COMPILE_LANGUAGE:C,CXX>:/FI${_root}/include/.internal/winsdk/${CMAKE_VS_WINDOWS_TARGET_PLATFORM_VERSION}/wdk/${WDK_VERSION}/forced.h>")
    endif()

    target_compile_options(${_target} PRIVATE "$<$<COMPILE_LANGUAGE:C,CXX>:/FI${_root}/include/.internal/adjust_link_order>")

    if(CRTSYS_NTL_MAIN)
      target_compile_definitions(${_target} PUBLIC CRTSYS_USE_NTL_MAIN)
    endif()
endfunction()

function(crtsys_link_prebuilt_driver_libraries _target)
    crtsys_get_prebuilt_library(_crtsys_debug crtsys.lib Debug)
    crtsys_get_prebuilt_library(_ldk_debug Ldk.lib Debug)
    crtsys_get_prebuilt_library(_crtsys_release crtsys.lib Release)
    crtsys_get_prebuilt_library(_ldk_release Ldk.lib Release)

    target_link_libraries(
        ${_target}
        PRIVATE
            debug "${_crtsys_debug}"
            debug "${_ldk_debug}"
            optimized "${_crtsys_release}"
            optimized "${_ldk_release}"
    )

    target_compile_definitions(${_target} PUBLIC "_KERNEL32_" "_ITERATOR_DEBUG_LEVEL=0" "_HAS_EXCEPTIONS")
    target_compile_options(${_target} PUBLIC "$<$<COMPILE_LANGUAGE:CXX>:/Zc:threadSafeInit->")
    target_compile_options(${_target} PUBLIC "$<$<COMPILE_LANGUAGE:C,CXX>:/MT>")

    if(CRTSYS_USE_LIBCNTPR)
        if(NOT TARGET WDK::LIBCNTPR)
            message(FATAL_ERROR "WDK::LIBCNTPR is required for crtsys prebuilt driver support.")
        endif()

        target_link_libraries(${_target} PRIVATE WDK::LIBCNTPR)
        target_compile_definitions(${_target} PUBLIC CRTSYS_USE_LIBCNTPR)
        target_link_options(${_target} PUBLIC "/FORCE:MULTIPLE")
    endif()
endfunction()

function(crtsys_add_driver _target)
    cmake_parse_arguments(WDK "" "WINVER" "" ${ARGN})
    wdk_add_driver(${_target} ${WDK_UNPARSED_ARGUMENTS} CUSTOM_ENTRY_POINT CrtSysDriverEntry EXTENDED_CPP_FEATURES)
    crtsys_scope_compile_options_to_c_cxx(${_target})

    crtsys_apply_driver_settings(${_target} "${_CRTSYS_ROOT}")

    if(CRTSYS_USE_PREBUILT)
        crtsys_link_prebuilt_driver_libraries(${_target})
    else()
        if(NOT TARGET crtsys)
            message(FATAL_ERROR "crtsys target was not found. Add crtsys with CPMAddPackage or use an unpacked crtsys native release bundle.")
        endif()

        target_link_libraries(${_target} crtsys)
    endif()
endfunction()
