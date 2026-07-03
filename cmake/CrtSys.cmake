cmake_policy(SET CMP0021 NEW)

set(CMAKE_CXX_STANDARD_LIBRARIES " ")
set(CMAKE_C_STANDARD_LIBRARIES ${CMAKE_CXX_STANDARD_LIBRARIES})
set(CMAKE_MODULE_PATH "${CMAKE_CURRENT_LIST_DIR};${CMAKE_MODULE_PATH}")

if(NOT DEFINED CRTSYS_NTL_MAIN)
    set(CRTSYS_NTL_MAIN ON)
endif()

if(NOT DEFINED CRTSYS_USE_LIBCNTPR)
    set(CRTSYS_USE_LIBCNTPR ON)
endif()

if(DEFINED crtsys_SOURCE_DIR)
    set(_CRTSYS_ROOT "${crtsys_SOURCE_DIR}")
elseif(DEFINED crtsys_ROOT)
    set(_CRTSYS_ROOT "${crtsys_ROOT}")
elseif(DEFINED CRTSYS_ROOT)
    set(_CRTSYS_ROOT "${CRTSYS_ROOT}")
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

function(crtsys_get_msvc_sdk_include_dirs OUT_VAR)
    set(INCLUDE_DIRS)

    if(MSVC AND CMAKE_CXX_COMPILER)
        get_filename_component(MSVC_COMPILER_ARCH_DIR "${CMAKE_CXX_COMPILER}" DIRECTORY)
        get_filename_component(MSVC_COMPILER_HOST_DIR "${MSVC_COMPILER_ARCH_DIR}" DIRECTORY)
        get_filename_component(MSVC_COMPILER_BIN_DIR "${MSVC_COMPILER_HOST_DIR}" DIRECTORY)
        get_filename_component(MSVC_TOOLS_DIR "${MSVC_COMPILER_BIN_DIR}" DIRECTORY)
        get_filename_component(MSVC_VC_DIR "${MSVC_TOOLS_DIR}/../../.." ABSOLUTE)

        if(EXISTS "${MSVC_TOOLS_DIR}/include")
            list(APPEND INCLUDE_DIRS "${MSVC_TOOLS_DIR}/include")
        endif()

        if(EXISTS "${MSVC_VC_DIR}/Auxiliary/VS/include")
            list(APPEND INCLUDE_DIRS "${MSVC_VC_DIR}/Auxiliary/VS/include")
        endif()
    endif()

    if(DEFINED WDK_ROOT AND DEFINED CMAKE_VS_WINDOWS_TARGET_PLATFORM_VERSION)
        foreach(SDK_INCLUDE_KIND ucrt um shared winrt cppwinrt)
            set(SDK_INCLUDE_DIR "${WDK_ROOT}/Include/${CMAKE_VS_WINDOWS_TARGET_PLATFORM_VERSION}/${SDK_INCLUDE_KIND}")
            if(EXISTS "${SDK_INCLUDE_DIR}")
                list(APPEND INCLUDE_DIRS "${SDK_INCLUDE_DIR}")
            endif()
        endforeach()
    endif()

    set(${OUT_VAR} "${INCLUDE_DIRS}" PARENT_SCOPE)
endfunction()

function(crtsys_generate_msvc_future_overlay OUT_VAR)
    set(${OUT_VAR} "" PARENT_SCOPE)

    if(NOT MSVC OR NOT CMAKE_CXX_COMPILER)
        return()
    endif()

    get_filename_component(MSVC_COMPILER_ARCH_DIR "${CMAKE_CXX_COMPILER}" DIRECTORY)
    get_filename_component(MSVC_COMPILER_HOST_DIR "${MSVC_COMPILER_ARCH_DIR}" DIRECTORY)
    get_filename_component(MSVC_COMPILER_BIN_DIR "${MSVC_COMPILER_HOST_DIR}" DIRECTORY)
    get_filename_component(MSVC_TOOLS_DIR "${MSVC_COMPILER_BIN_DIR}" DIRECTORY)
    set(MSVC_FUTURE_HEADER "${MSVC_TOOLS_DIR}/include/future")
    if(NOT EXISTS "${MSVC_FUTURE_HEADER}")
        return()
    endif()

    file(READ "${MSVC_FUTURE_HEADER}" CRTSYS_MSVC_FUTURE_CONTENT)

    set(CRTSYS_FUTURE_IS_READY_NOEXCEPT_SNIPPET
"    bool _Is_ready() const noexcept {
        return _State._Is_ready();
    }

")
    set(CRTSYS_FUTURE_READY_OR_STORED_NOEXCEPT_SNIPPET
"    bool _Is_ready() const noexcept {
        return _State._Is_ready();
    }

    bool _Already_has_stored_result() const noexcept {
        return _State._Ptr() && _State._Ptr()->_Already_has_stored_result();
    }

")
    set(CRTSYS_FUTURE_IS_READY_SNIPPET
"    bool _Is_ready() const {
        return _State._Is_ready();
    }

")
    set(CRTSYS_FUTURE_READY_OR_STORED_SNIPPET
"    bool _Is_ready() const {
        return _State._Is_ready();
    }

    bool _Already_has_stored_result() const {
        return _State._Ptr() && _State._Ptr()->_Already_has_stored_result();
    }

")

    set(CRTSYS_FUTURE_IS_READY_INDEX -1)
    string(FIND "${CRTSYS_MSVC_FUTURE_CONTENT}" "${CRTSYS_FUTURE_IS_READY_NOEXCEPT_SNIPPET}" CRTSYS_FUTURE_IS_READY_NOEXCEPT_INDEX)
    if(NOT CRTSYS_FUTURE_IS_READY_NOEXCEPT_INDEX EQUAL -1)
        string(REPLACE "${CRTSYS_FUTURE_IS_READY_NOEXCEPT_SNIPPET}" "${CRTSYS_FUTURE_READY_OR_STORED_NOEXCEPT_SNIPPET}" CRTSYS_MSVC_FUTURE_CONTENT "${CRTSYS_MSVC_FUTURE_CONTENT}")
    else()
        string(FIND "${CRTSYS_MSVC_FUTURE_CONTENT}" "${CRTSYS_FUTURE_IS_READY_SNIPPET}" CRTSYS_FUTURE_IS_READY_INDEX)
    endif()
    if(CRTSYS_FUTURE_IS_READY_NOEXCEPT_INDEX EQUAL -1 AND CRTSYS_FUTURE_IS_READY_INDEX EQUAL -1)
        message(WARNING "Unable to patch MSVC <future>: _Promise::_Is_ready shape was not recognized.")
        return()
    endif()
    if(NOT CRTSYS_FUTURE_IS_READY_INDEX EQUAL -1)
        string(REPLACE "${CRTSYS_FUTURE_IS_READY_SNIPPET}" "${CRTSYS_FUTURE_READY_OR_STORED_SNIPPET}" CRTSYS_MSVC_FUTURE_CONTENT "${CRTSYS_MSVC_FUTURE_CONTENT}")
    endif()

    set(CRTSYS_FUTURE_PROMISE_DTOR_CONDITION "if (_MyPromise._Is_valid() && !_MyPromise._Is_ready())")
    set(CRTSYS_FUTURE_PROMISE_DTOR_AT_THREAD_EXIT_CONDITION "if (_MyPromise._Is_valid() && !_MyPromise._Is_ready() && !_MyPromise._Is_ready_at_thread_exit())")
    set(CRTSYS_FUTURE_DTOR_INDEX -1)
    set(CRTSYS_FUTURE_DTOR_AT_THREAD_EXIT_INDEX -1)
    string(FIND "${CRTSYS_MSVC_FUTURE_CONTENT}" "${CRTSYS_FUTURE_PROMISE_DTOR_CONDITION}" CRTSYS_FUTURE_DTOR_INDEX)
    string(FIND "${CRTSYS_MSVC_FUTURE_CONTENT}" "${CRTSYS_FUTURE_PROMISE_DTOR_AT_THREAD_EXIT_CONDITION}" CRTSYS_FUTURE_DTOR_AT_THREAD_EXIT_INDEX)
    if(CRTSYS_FUTURE_DTOR_INDEX EQUAL -1 AND CRTSYS_FUTURE_DTOR_AT_THREAD_EXIT_INDEX EQUAL -1)
        message(WARNING "Unable to patch MSVC <future>: promise destructor shape was not recognized.")
        return()
    endif()

    # MSVC STL keeps set_value_at_thread_exit() states unready until the CRT
    # thread-exit callback broadcasts. A promise destructor must not translate
    # that already-stored-but-not-yet-ready value into broken_promise.
    if(NOT CRTSYS_FUTURE_DTOR_AT_THREAD_EXIT_INDEX EQUAL -1)
        string(REPLACE
            "${CRTSYS_FUTURE_PROMISE_DTOR_AT_THREAD_EXIT_CONDITION}"
            "if (_MyPromise._Is_valid() && !_MyPromise._Is_ready() && !_MyPromise._Is_ready_at_thread_exit() && !_MyPromise._Already_has_stored_result())"
            CRTSYS_MSVC_FUTURE_CONTENT
            "${CRTSYS_MSVC_FUTURE_CONTENT}")
    endif()
    if(NOT CRTSYS_FUTURE_DTOR_INDEX EQUAL -1)
        string(REPLACE
            "${CRTSYS_FUTURE_PROMISE_DTOR_CONDITION}"
            "if (_MyPromise._Is_valid() && !_MyPromise._Is_ready() && !_MyPromise._Already_has_stored_result())"
            CRTSYS_MSVC_FUTURE_CONTENT
            "${CRTSYS_MSVC_FUTURE_CONTENT}")
    endif()

    set(CRTSYS_MSVC_FUTURE_OVERLAY_DIR "${CMAKE_CURRENT_BINARY_DIR}/crtsys-msvc-overlay/${MSVC_TOOLSET_VERSION}")
    file(MAKE_DIRECTORY "${CRTSYS_MSVC_FUTURE_OVERLAY_DIR}")
    file(WRITE "${CRTSYS_MSVC_FUTURE_OVERLAY_DIR}/future" "${CRTSYS_MSVC_FUTURE_CONTENT}")
    set(${OUT_VAR} "${CRTSYS_MSVC_FUTURE_OVERLAY_DIR}" PARENT_SCOPE)
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

set(CRTSYS_CPM_DOWNLOAD_URL "https://github.com/cpm-cmake/CPM.cmake/releases/download/v${CPM_DOWNLOAD_VERSION}/CPM.cmake")

function(crtsys_validate_cpm_download cpm_path out_var)
    set(cpm_is_valid FALSE)
    if(EXISTS "${cpm_path}")
        file(READ "${cpm_path}" cpm_download_content LIMIT 1048576)
        string(FIND "${cpm_download_content}" "CPMAddPackage" cpm_add_package_index)
        if(NOT cpm_add_package_index EQUAL -1)
            set(cpm_is_valid TRUE)
        endif()
    endif()

    set("${out_var}" "${cpm_is_valid}" PARENT_SCOPE)
endfunction()

function(crtsys_download_cpm cpm_url cpm_path)
    message(STATUS "Downloading CPM.cmake to ${cpm_path}")
    get_filename_component(CPM_DOWNLOAD_DIRECTORY "${cpm_path}" DIRECTORY)
    file(MAKE_DIRECTORY "${CPM_DOWNLOAD_DIRECTORY}")
    file(DOWNLOAD
        "${cpm_url}"
        "${cpm_path}"
        STATUS CPM_DOWNLOAD_STATUS
        LOG CPM_DOWNLOAD_LOG
        TLS_VERIFY ON
    )
    list(GET CPM_DOWNLOAD_STATUS 0 CPM_DOWNLOAD_STATUS_CODE)
    list(GET CPM_DOWNLOAD_STATUS 1 CPM_DOWNLOAD_STATUS_MESSAGE)
    if(NOT CPM_DOWNLOAD_STATUS_CODE EQUAL 0)
        message(FATAL_ERROR "Failed to download CPM.cmake: ${CPM_DOWNLOAD_STATUS_MESSAGE}\n${CPM_DOWNLOAD_LOG}")
    endif()
endfunction()

crtsys_validate_cpm_download("${CPM_DOWNLOAD_LOCATION}" CPM_DOWNLOAD_VALID)
if(NOT CPM_DOWNLOAD_VALID)
    if(EXISTS "${CPM_DOWNLOAD_LOCATION}")
        message(WARNING "Existing CPM.cmake does not look valid; re-downloading ${CPM_DOWNLOAD_LOCATION}")
        file(REMOVE "${CPM_DOWNLOAD_LOCATION}")
    endif()

    crtsys_download_cpm("${CRTSYS_CPM_DOWNLOAD_URL}" "${CPM_DOWNLOAD_LOCATION}")
    crtsys_validate_cpm_download("${CPM_DOWNLOAD_LOCATION}" CPM_DOWNLOAD_VALID)
    if(NOT CPM_DOWNLOAD_VALID)
        message(FATAL_ERROR "Downloaded CPM.cmake does not contain CPMAddPackage: ${CPM_DOWNLOAD_LOCATION}")
    endif()
endif()

include("${CPM_DOWNLOAD_LOCATION}")
if(NOT COMMAND CPMAddPackage)
    message(FATAL_ERROR "Downloaded CPM.cmake did not define CPMAddPackage: ${CPM_DOWNLOAD_LOCATION}")
endif()
#---------------------------------------------------------------------------------------------------



CPMAddPackage("gh:ntoskrnl7/FindWDK#master")
list(APPEND CMAKE_MODULE_PATH "${FindWDK_SOURCE_DIR}/cmake")
find_package(WDK REQUIRED)

function(crtsys_get_prebuilt_arch _out_arch)
    if("${CMAKE_VS_PLATFORM_NAME}" STREQUAL "x64")
        set(_arch x64)
    elseif("${CMAKE_VS_PLATFORM_NAME}" STREQUAL "ARM64")
        set(_arch ARM64)
    elseif("${CMAKE_VS_PLATFORM_NAME}" STREQUAL "ARM")
        set(_arch ARM)
    elseif("${CMAKE_VS_PLATFORM_NAME}" STREQUAL "Win32")
        set(_arch x86)
    else()
        message(FATAL_ERROR "Unsupported crtsys prebuilt platform: ${CMAKE_VS_PLATFORM_NAME}")
    endif()

    set(${_out_arch} "${_arch}" PARENT_SCOPE)
endfunction()

function(crtsys_get_prebuilt_toolset _out_toolset)
    if(DEFINED CRTSYS_PREBUILT_TOOLSET AND NOT "${CRTSYS_PREBUILT_TOOLSET}" STREQUAL "")
        set(_toolset "${CRTSYS_PREBUILT_TOOLSET}")
    elseif(DEFINED MSVC_TOOLSET_VERSION)
        set(_toolset "v${MSVC_TOOLSET_VERSION}")
    else()
        set(_toolset "")
    endif()

    if("${_toolset}" STREQUAL "")
        message(FATAL_ERROR "Unable to determine the crtsys prebuilt MSVC toolset. Set CRTSYS_PREBUILT_TOOLSET to v142, v143, or v145.")
    endif()

    set(${_out_toolset} "${_toolset}" PARENT_SCOPE)
endfunction()

function(crtsys_get_prebuilt_library _out_path _library _configuration)
    crtsys_get_prebuilt_arch(_arch)
    crtsys_get_prebuilt_toolset(_toolset)

    if("${_configuration}" STREQUAL "Debug")
        set(_config_dir Debug)
    else()
        set(_config_dir Release)
    endif()

    set(_has_toolset_layout FALSE)
    foreach(_known_toolset v142 v143 v145)
        if(EXISTS "${_CRTSYS_ROOT}/lib/native/${_known_toolset}")
            set(_has_toolset_layout TRUE)
        endif()
    endforeach()

    set(_candidate_paths
        "${_CRTSYS_ROOT}/lib/native/${_toolset}/${_arch}/${_config_dir}/${_library}"
    )

    if(DEFINED CRTSYS_ALLOW_PREBUILT_TOOLSET_FALLBACK AND CRTSYS_ALLOW_PREBUILT_TOOLSET_FALLBACK)
        list(APPEND _candidate_paths
            "${_CRTSYS_ROOT}/lib/native/v143/${_arch}/${_config_dir}/${_library}"
        )
    endif()

    if(NOT _has_toolset_layout)
        list(APPEND _candidate_paths
            "${_CRTSYS_ROOT}/lib/native/${_arch}/${_config_dir}/${_library}"
        )
    endif()

    foreach(_path IN LISTS _candidate_paths)
        file(TO_CMAKE_PATH "${_path}" _path)
        if(EXISTS "${_path}")
            set(${_out_path} "${_path}" PARENT_SCOPE)
            return()
        endif()
    endforeach()

    if(_has_toolset_layout)
        set(_missing_hint "Expected ${_CRTSYS_ROOT}/lib/native/${_toolset}/${_arch}/${_config_dir}/${_library}.")
    else()
        set(_missing_hint "Expected ${_CRTSYS_ROOT}/lib/native/${_arch}/${_config_dir}/${_library}.")
    endif()

    if("${_library}" STREQUAL "crtsys.lib")
        message(STATUS "crtsys prebuilt library was not found for toolset ${_toolset}, platform ${_arch}, config ${_config_dir}. ${_missing_hint}")
    endif()

    set(${_out_path} "" PARENT_SCOPE)
    return()
endfunction()

function(crtsys_apply_driver_settings _target _root)
    get_target_property(INC_DIR_TMP ${_target} INCLUDE_DIRECTORIES)
    if(NOT INC_DIR_TMP)
        set(INC_DIR_TMP "")
    endif()

    crtsys_get_msvc_sdk_include_dirs(_crtsys_msvc_sdk_include_dirs)
    crtsys_generate_msvc_future_overlay(_crtsys_msvc_future_overlay_dir)
    set(_crtsys_msvc_compat_toolset "${MSVC_TOOLSET_VERSION}")
    if(MSVC_TOOLSET_VERSION GREATER 143)
        set(_crtsys_msvc_compat_toolset 143)
    endif()
    set(_crtsys_include_dirs
        "${_crtsys_msvc_future_overlay_dir}"
        "${_root}/include"
        "${_root}/include/.internal/msvc/${_crtsys_msvc_compat_toolset}"
        "${_root}/include/.internal/msvc/${_crtsys_msvc_compat_toolset}/stl"
        ${_crtsys_msvc_sdk_include_dirs}
        ${INC_DIR_TMP}
    )
    set_property(TARGET ${_target} PROPERTY INCLUDE_DIRECTORIES ${_crtsys_include_dirs})

    set(_crtsys_winsdk_forced_include "${_root}/include/.internal/winsdk/${CMAKE_VS_WINDOWS_TARGET_PLATFORM_VERSION}/wdk/${WDK_VERSION}/forced.h")
    if(NOT EXISTS "${_crtsys_winsdk_forced_include}")
      set(_crtsys_winsdk_forced_include "${_root}/include/.internal/winsdk/wdk/${WDK_VERSION}/forced.h")
    endif()
    if(EXISTS "${_crtsys_winsdk_forced_include}")
      target_compile_options(${_target} PRIVATE "$<$<COMPILE_LANGUAGE:C,CXX>:/FI${_crtsys_winsdk_forced_include}>")
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

    if((_crtsys_debug AND NOT _ldk_debug) OR (_ldk_debug AND NOT _crtsys_debug))
        message(FATAL_ERROR "The crtsys Debug prebuilt library pair is incomplete under ${_CRTSYS_ROOT}/lib/native.")
    endif()
    if((_crtsys_release AND NOT _ldk_release) OR (_ldk_release AND NOT _crtsys_release))
        message(FATAL_ERROR "The crtsys Release prebuilt library pair is incomplete under ${_CRTSYS_ROOT}/lib/native.")
    endif()

    if(_crtsys_debug AND _crtsys_release)
        target_link_libraries(
            ${_target}
            debug "${_crtsys_debug}"
            debug "${_ldk_debug}"
            optimized "${_crtsys_release}"
            optimized "${_ldk_release}"
        )
    elseif(_crtsys_debug)
        target_link_libraries(${_target} "${_crtsys_debug}" "${_ldk_debug}")
    elseif(_crtsys_release)
        target_link_libraries(${_target} "${_crtsys_release}" "${_ldk_release}")
    else()
        crtsys_get_prebuilt_toolset(_toolset)
        message(FATAL_ERROR "No crtsys prebuilt libraries were found under ${_CRTSYS_ROOT}/lib/native for ${_toolset}/${CMAKE_VS_PLATFORM_NAME}.")
    endif()

    target_compile_definitions(${_target} PUBLIC "_KERNEL32_" "_ITERATOR_DEBUG_LEVEL=0" "_HAS_EXCEPTIONS")
    target_compile_options(${_target} PUBLIC "$<$<COMPILE_LANGUAGE:C,CXX>:/MT>")

    if(CRTSYS_USE_LIBCNTPR)
        if(NOT TARGET WDK::LIBCNTPR)
            message(FATAL_ERROR "WDK::LIBCNTPR is required for crtsys prebuilt driver support.")
        endif()

        target_link_libraries(${_target} WDK::LIBCNTPR)
        target_compile_definitions(${_target} PUBLIC CRTSYS_USE_LIBCNTPR)
        target_link_options(${_target} PUBLIC "/FORCE:MULTIPLE")
    endif()

    if("${CMAKE_VS_PLATFORM_NAME}" STREQUAL "Win32")
        target_link_options(${_target} PUBLIC "/SAFESEH:NO")
    endif()
endfunction()

function(crtsys_add_driver _target)
    cmake_parse_arguments(WDK "" "WINVER" "" ${ARGN})

    set(_CRTSYS_ORIGINAL_GENERATOR_PLATFORM "${CMAKE_GENERATOR_PLATFORM}")
    if(_CRTSYS_ORIGINAL_GENERATOR_PLATFORM MATCHES "^([^,]+),")
        set(CMAKE_GENERATOR_PLATFORM "${CMAKE_MATCH_1}")
    endif()

    wdk_add_driver(${_target} ${WDK_UNPARSED_ARGUMENTS} CUSTOM_ENTRY_POINT CrtSysDriverEntry EXTENDED_CPP_FEATURES)

    set(CMAKE_GENERATOR_PLATFORM "${_CRTSYS_ORIGINAL_GENERATOR_PLATFORM}")
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
