cmake_policy(SET CMP0021 NEW)

set(_CRTSYS_CMAKE_MODULE_DIR "${CMAKE_CURRENT_LIST_DIR}")
include("${_CRTSYS_CMAKE_MODULE_DIR}/NtlMsQuic.cmake")

set(CMAKE_CXX_STANDARD_LIBRARIES " ")
set(CMAKE_C_STANDARD_LIBRARIES ${CMAKE_CXX_STANDARD_LIBRARIES})
set(CMAKE_MODULE_PATH "${CMAKE_CURRENT_LIST_DIR};${CMAKE_MODULE_PATH}")

if(NOT DEFINED CRTSYS_NTL_MAIN)
    set(CRTSYS_NTL_MAIN ON)
endif()

if(NOT DEFINED CRTSYS_USE_LIBCNTPR)
    set(CRTSYS_USE_LIBCNTPR ON)
endif()

if(DEFINED CRTSYS_ROOT AND CRTSYS_USE_PREBUILT)
    # An explicit prebuilt root must win over crtsys_SOURCE_DIR. Source-tree
    # consumers still set the latter through CPM even when the native archives
    # live in a separate staged bundle.
    set(_CRTSYS_ROOT "${CRTSYS_ROOT}")
elseif(DEFINED crtsys_SOURCE_DIR)
    set(_CRTSYS_ROOT "${crtsys_SOURCE_DIR}")
elseif(DEFINED crtsys_ROOT)
    set(_CRTSYS_ROOT "${crtsys_ROOT}")
elseif(DEFINED CRTSYS_ROOT)
    set(_CRTSYS_ROOT "${CRTSYS_ROOT}")
else()
    get_filename_component(_CRTSYS_ROOT "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)
endif()

if(NOT DEFINED CRTSYS_USE_PREBUILT)
    if(NOT TARGET crtsys AND
       (EXISTS "${_CRTSYS_ROOT}/lib/native" OR
        EXISTS "${_CRTSYS_ROOT}/lib/manual-link" OR
        EXISTS "${_CRTSYS_ROOT}/debug/lib/manual-link"))
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

# FindWDK is vendored at a pinned upstream revision under
# cmake/vendor/findwdk. The crtsys package must remain fully usable without
# downloading build helpers while a consumer runs find_package(crtsys).
find_package(WDK REQUIRED)

function(crtsys_get_prebuilt_arch _out_arch)
    if(DEFINED CRTSYS_TARGET_ARCHITECTURE AND
       NOT "${CRTSYS_TARGET_ARCHITECTURE}" STREQUAL "")
        set(_arch "${CRTSYS_TARGET_ARCHITECTURE}")
    elseif("${CMAKE_VS_PLATFORM_NAME}" STREQUAL "x64")
        set(_arch x64)
    elseif("${CMAKE_VS_PLATFORM_NAME}" STREQUAL "ARM64")
        set(_arch ARM64)
    elseif("${CMAKE_VS_PLATFORM_NAME}" STREQUAL "ARM")
        set(_arch ARM)
    elseif("${CMAKE_VS_PLATFORM_NAME}" STREQUAL "Win32")
        set(_arch x86)
    elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "^(ARM64|arm64|aarch64)$")
        set(_arch ARM64)
    elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "^(ARM|arm)$")
        set(_arch ARM)
    elseif(CMAKE_SIZEOF_VOID_P EQUAL 8)
        set(_arch x64)
    elseif(CMAKE_SIZEOF_VOID_P EQUAL 4)
        set(_arch x86)
    else()
        message(FATAL_ERROR
            "Unable to determine the crtsys target architecture. Set "
            "CRTSYS_TARGET_ARCHITECTURE to x86, x64, ARM, or ARM64.")
    endif()

    string(TOLOWER "${_arch}" _arch_lower)
    if(_arch_lower STREQUAL "win32" OR _arch_lower STREQUAL "x86")
        set(_arch x86)
    elseif(_arch_lower STREQUAL "x64" OR _arch_lower STREQUAL "amd64")
        set(_arch x64)
    elseif(_arch_lower STREQUAL "arm")
        set(_arch ARM)
    elseif(_arch_lower STREQUAL "arm64" OR _arch_lower STREQUAL "aarch64")
        set(_arch ARM64)
    else()
        message(FATAL_ERROR "Unsupported crtsys target architecture: ${_arch}")
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
    if("${_configuration}" STREQUAL "Debug")
        set(_config_dir Debug)
        set(_standard_library
            "${_CRTSYS_ROOT}/debug/lib/manual-link/${_library}")
    else()
        set(_config_dir Release)
        set(_standard_library
            "${_CRTSYS_ROOT}/lib/manual-link/${_library}")
    endif()

    file(TO_CMAKE_PATH "${_standard_library}" _standard_library)
    if(EXISTS "${_standard_library}")
        set(${_out_path} "${_standard_library}" PARENT_SCOPE)
        return()
    endif()

    crtsys_get_prebuilt_arch(_arch)
    crtsys_get_prebuilt_toolset(_toolset)

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

function(crtsys_apply_driver_settings _target _root _use_ntl_main _use_ntl_kmdf_main _use_ntl_flt_main)
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

    if(_use_ntl_main)
      target_compile_definitions(${_target} PUBLIC CRTSYS_USE_NTL_MAIN)
    endif()
    if(_use_ntl_kmdf_main)
      target_compile_definitions(${_target} PUBLIC CRTSYS_USE_NTL_KMDF_MAIN)
    endif()
    if(_use_ntl_flt_main)
      target_compile_definitions(${_target} PUBLIC CRTSYS_USE_NTL_FLT_MAIN)
    endif()
endfunction()

function(crtsys_apply_prebuilt_driver_interface _target)
    get_target_property(_target_type ${_target} TYPE)
    if(_target_type STREQUAL "INTERFACE_LIBRARY")
        set(_usage_requirement_scope INTERFACE)
    else()
        set(_usage_requirement_scope PUBLIC)
    endif()

    crtsys_get_prebuilt_library(_crtsys_debug crtsys.lib Debug)
    crtsys_get_prebuilt_library(_ldk_debug Ldk.lib Debug)
    crtsys_get_prebuilt_library(_crtsys_release crtsys.lib Release)
    crtsys_get_prebuilt_library(_ldk_release Ldk.lib Release)

    if((_crtsys_debug AND NOT _ldk_debug) OR (_ldk_debug AND NOT _crtsys_debug))
        message(FATAL_ERROR "The crtsys Debug library pair is incomplete under ${_CRTSYS_ROOT}.")
    endif()
    if((_crtsys_release AND NOT _ldk_release) OR (_ldk_release AND NOT _crtsys_release))
        message(FATAL_ERROR "The crtsys Release library pair is incomplete under ${_CRTSYS_ROOT}.")
    endif()

    if(_crtsys_debug AND _crtsys_release)
        set(_crtsys_prebuilt_link_libraries
            "$<$<CONFIG:Debug>:${_crtsys_debug}>"
            "$<$<CONFIG:Debug>:${_ldk_debug}>"
            "$<$<NOT:$<CONFIG:Debug>>:${_crtsys_release}>"
            "$<$<NOT:$<CONFIG:Debug>>:${_ldk_release}>")
    elseif(_crtsys_debug)
        set(_crtsys_prebuilt_link_libraries
            "${_crtsys_debug}" "${_ldk_debug}")
    elseif(_crtsys_release)
        set(_crtsys_prebuilt_link_libraries
            "${_crtsys_release}" "${_ldk_release}")
    else()
        message(FATAL_ERROR
            "No crtsys libraries were found in the standard manual-link "
            "directories or the legacy native layout under ${_CRTSYS_ROOT}.")
    endif()

    foreach(_required_target IN ITEMS WDK::CNG WDK::AUX_KLIB WDK::WDMSEC)
        if(NOT TARGET ${_required_target})
            message(FATAL_ERROR "${_required_target} is required for crtsys prebuilt driver support.")
        endif()
        list(APPEND _crtsys_prebuilt_link_libraries ${_required_target})
    endforeach()

    if(CRTSYS_USE_LIBCNTPR)
        if(NOT TARGET WDK::LIBCNTPR)
            message(FATAL_ERROR "WDK::LIBCNTPR is required for crtsys prebuilt driver support.")
        endif()
        list(APPEND _crtsys_prebuilt_link_libraries WDK::LIBCNTPR)
    endif()

    if(_target_type STREQUAL "INTERFACE_LIBRARY")
        target_link_libraries(
            ${_target} INTERFACE ${_crtsys_prebuilt_link_libraries})
    else()
        # FindWDK uses the plain target_link_libraries signature for driver
        # targets, so keep using that signature here as well.
        target_link_libraries(${_target} ${_crtsys_prebuilt_link_libraries})
    endif()
    target_compile_definitions(
        ${_target} ${_usage_requirement_scope}
        "_KERNEL32_" "_ITERATOR_DEBUG_LEVEL=0" "_HAS_EXCEPTIONS")
    target_compile_options(
        ${_target} ${_usage_requirement_scope}
        "$<$<COMPILE_LANGUAGE:C,CXX>:/MT>")

    if(CRTSYS_USE_LIBCNTPR)
        target_compile_definitions(
            ${_target} ${_usage_requirement_scope} CRTSYS_USE_LIBCNTPR)
        target_link_options(
            ${_target} ${_usage_requirement_scope} "/FORCE:MULTIPLE")
    endif()
    crtsys_get_prebuilt_arch(_crtsys_target_arch)
    if(_crtsys_target_arch STREQUAL "x86")
        target_link_options(
            ${_target} ${_usage_requirement_scope} "/SAFESEH:NO")
    endif()
endfunction()

function(crtsys_link_prebuilt_driver_libraries _target)
    crtsys_apply_prebuilt_driver_interface(${_target})
endfunction()

if(CRTSYS_USE_PREBUILT)
    # Source-tree CPM consumers historically link the `crtsys` target from
    # auxiliary kernel targets. Keep that target contract while redirecting
    # it to the shared archives prepared by CI.
    if(NOT TARGET crtsys)
        add_library(crtsys INTERFACE IMPORTED GLOBAL)
        crtsys_apply_prebuilt_driver_interface(crtsys)
    endif()
endif()

function(crtsys_add_driver _target)
    cmake_parse_arguments(WDK "NTL;MINIFILTER;WFP;KERNEL_MSQUIC;KERNEL_CONTENT_CODECS" "WINVER;KMDF" "" ${ARGN})

    if((WDK_MINIFILTER AND WDK_KMDF) OR
       (WDK_MINIFILTER AND WDK_WFP) OR
       (WDK_KMDF AND WDK_WFP))
        message(FATAL_ERROR "MINIFILTER, KMDF, and WFP select different driver models and cannot be combined.")
    endif()
    if(WDK_NTL AND NOT WDK_KMDF AND NOT WDK_MINIFILTER AND NOT WDK_WFP)
        message(FATAL_ERROR "The NTL argument is valid with KMDF, MINIFILTER, or WFP. Use CRTSYS_NTL_MAIN for WDM NTL drivers.")
    endif()

    set(_crtsys_wdk_arguments ${WDK_UNPARSED_ARGUMENTS})
    # FindWDK also defines a cache variable named WDK_WINVER. Determine
    # whether this function actually received the WINVER keyword instead of
    # mistaking that global default for a per-target argument.
    list(FIND ARGN "WINVER" _crtsys_winver_argument_index)
    if(NOT _crtsys_winver_argument_index EQUAL -1)
        if(WDK_KERNEL_MSQUIC)
            math(EXPR _crtsys_kernel_msquic_winver "${WDK_WINVER}")
            if(_crtsys_kernel_msquic_winver LESS 0x0A00)
                message(FATAL_ERROR
                    "KERNEL_MSQUIC requires WINVER 0x0A00 or newer.")
            endif()
        endif()
        list(APPEND _crtsys_wdk_arguments WINVER "${WDK_WINVER}")
    elseif(WDK_KERNEL_MSQUIC)
        list(APPEND _crtsys_wdk_arguments WINVER "0x0A00")
    elseif(WDK_WFP)
        # ntl::wfp uses the version-2 callout contract introduced in Windows 8.
        list(APPEND _crtsys_wdk_arguments WINVER "0x0602")
    endif()

    if(WDK_MINIFILTER AND WDK_NTL)
        set(_crtsys_entry_point CrtSysNtlFltDriverEntry)
        set(_crtsys_use_ntl_main FALSE)
        set(_crtsys_use_ntl_kmdf_main FALSE)
        set(_crtsys_use_ntl_flt_main TRUE)
    elseif(WDK_MINIFILTER)
        set(_crtsys_entry_point CrtSysWdmDriverEntry)
        set(_crtsys_use_ntl_main FALSE)
        set(_crtsys_use_ntl_kmdf_main FALSE)
        set(_crtsys_use_ntl_flt_main FALSE)
    elseif(WDK_KMDF AND WDK_NTL)
        set(_crtsys_entry_point CrtSysNtlKmdfDriverEntry)
        set(_crtsys_use_ntl_main FALSE)
        set(_crtsys_use_ntl_kmdf_main TRUE)
        set(_crtsys_use_ntl_flt_main FALSE)
    elseif(WDK_KMDF)
        set(_crtsys_entry_point CrtSysKmdfDriverEntry)
        set(_crtsys_use_ntl_main FALSE)
        set(_crtsys_use_ntl_kmdf_main FALSE)
        set(_crtsys_use_ntl_flt_main FALSE)
    elseif(WDK_WFP AND WDK_NTL)
        set(_crtsys_entry_point CrtSysDriverEntry)
        set(_crtsys_use_ntl_main TRUE)
        set(_crtsys_use_ntl_kmdf_main FALSE)
        set(_crtsys_use_ntl_flt_main FALSE)
    elseif(WDK_WFP)
        set(_crtsys_entry_point CrtSysWdmDriverEntry)
        set(_crtsys_use_ntl_main FALSE)
        set(_crtsys_use_ntl_kmdf_main FALSE)
        set(_crtsys_use_ntl_flt_main FALSE)
    elseif(CRTSYS_NTL_MAIN)
        set(_crtsys_entry_point CrtSysDriverEntry)
        set(_crtsys_use_ntl_main TRUE)
        set(_crtsys_use_ntl_kmdf_main FALSE)
        set(_crtsys_use_ntl_flt_main FALSE)
    else()
        set(_crtsys_entry_point CrtSysWdmDriverEntry)
        set(_crtsys_use_ntl_main FALSE)
        set(_crtsys_use_ntl_kmdf_main FALSE)
        set(_crtsys_use_ntl_flt_main FALSE)
    endif()

    set(_CRTSYS_ORIGINAL_GENERATOR_PLATFORM "${CMAKE_GENERATOR_PLATFORM}")
    if(_CRTSYS_ORIGINAL_GENERATOR_PLATFORM MATCHES "^([^,]+),")
        set(CMAKE_GENERATOR_PLATFORM "${CMAKE_MATCH_1}")
    endif()

    wdk_add_driver(
        ${_target}
        ${_crtsys_wdk_arguments}
        CUSTOM_ENTRY_POINT ${_crtsys_entry_point}
        EXTENDED_CPP_FEATURES
    )

    if(WDK_KMDF)
        set(_crtsys_kmdf_include "${WDK_ROOT}/Include/wdf/kmdf/${WDK_KMDF}")
        set(_crtsys_kmdf_lib "${WDK_ROOT}/Lib/wdf/kmdf/${WDK_PLATFORM}/${WDK_KMDF}")
        if(NOT EXISTS "${_crtsys_kmdf_include}/wdf.h")
            message(FATAL_ERROR "KMDF ${WDK_KMDF} headers were not found under ${_crtsys_kmdf_include}.")
        endif()
        if(NOT EXISTS "${_crtsys_kmdf_lib}/WdfDriverEntry.lib" OR
           NOT EXISTS "${_crtsys_kmdf_lib}/WdfLdr.lib")
            message(FATAL_ERROR "KMDF ${WDK_KMDF} libraries were not found under ${_crtsys_kmdf_lib}.")
        endif()

        target_include_directories(${_target} SYSTEM PRIVATE "${_crtsys_kmdf_include}")
        target_link_libraries(
            ${_target}
            "${_crtsys_kmdf_lib}/WdfDriverEntry.lib"
            "${_crtsys_kmdf_lib}/WdfLdr.lib"
        )
        target_compile_definitions(${_target} PUBLIC CRTSYS_USE_KMDF)
    endif()

    if(WDK_MINIFILTER)
        if(NOT TARGET WDK::FLTMGR)
            message(FATAL_ERROR "WDK::FLTMGR is required for minifilter drivers.")
        endif()
        target_link_libraries(${_target} WDK::FLTMGR)
        target_compile_definitions(${_target} PUBLIC CRTSYS_USE_MINIFILTER)
    endif()

    if(WDK_WFP)
        if(NOT TARGET WDK::FWPKCLNT)
            message(FATAL_ERROR "WDK::FWPKCLNT is required for WFP callout drivers.")
        endif()
        target_link_libraries(${_target} WDK::FWPKCLNT)
        if(WDK_PLATFORM MATCHES "^ARM")
            # Windows on ARM requires NDIS 6.30 or newer.
            set(_crtsys_wfp_ndis_version NDIS630)
        else()
            set(_crtsys_wfp_ndis_version NDIS60)
        endif()
        if(WDK_KERNEL_MSQUIC)
            set(_crtsys_wfp_ntddi NTDDI_WIN10_VB)
        else()
            set(_crtsys_wfp_ntddi NTDDI_WIN8)
        endif()
        target_compile_definitions(
            ${_target}
            PUBLIC
                CRTSYS_USE_WFP
                ${_crtsys_wfp_ndis_version}
                NDIS_SUPPORT_NDIS6
                NTDDI_VERSION=${_crtsys_wfp_ntddi}
        )
    elseif(WDK_KERNEL_MSQUIC)
        target_compile_definitions(
            ${_target} PUBLIC NTDDI_VERSION=NTDDI_WIN10_VB)
    endif()

    set(CMAKE_GENERATOR_PLATFORM "${_CRTSYS_ORIGINAL_GENERATOR_PLATFORM}")

    if(WDK_KERNEL_MSQUIC)
        if(NOT TARGET WDK::NETIO)
            message(FATAL_ERROR
                "KERNEL_MSQUIC requires the WDK::NETIO NMR client import target.")
        endif()
        crtsys_add_ntl_msquic_headers()
        target_link_libraries(
            ${_target} crtsys_ntl_msquic_headers WDK::NETIO)
    endif()

    if(WDK_KERNEL_CONTENT_CODECS)
        include("${_CRTSYS_CMAKE_MODULE_DIR}/NtlContentCodecs.cmake")
        crtsys_add_ntl_kernel_content_codecs()
        target_link_libraries(
            ${_target} crtsys_ntl_kernel_content_codecs)
    endif()

    crtsys_scope_compile_options_to_c_cxx(${_target})

    crtsys_apply_driver_settings(
        ${_target}
        "${_CRTSYS_ROOT}"
        ${_crtsys_use_ntl_main}
        ${_crtsys_use_ntl_kmdf_main}
        ${_crtsys_use_ntl_flt_main}
    )

    if(_crtsys_use_ntl_main OR _crtsys_use_ntl_kmdf_main OR _crtsys_use_ntl_flt_main)
        target_compile_features(${_target} PRIVATE cxx_std_20)
        set_target_properties(
            ${_target}
            PROPERTIES
                CXX_STANDARD 20
                CXX_STANDARD_REQUIRED YES
                CXX_EXTENSIONS NO
        )
        if(MSVC)
            target_compile_options(
                ${_target}
                PRIVATE "$<$<COMPILE_LANGUAGE:CXX>:/Zc:__cplusplus>"
            )
        endif()
    endif()

    if(CRTSYS_USE_PREBUILT)
        crtsys_link_prebuilt_driver_libraries(${_target})
    else()
        if(NOT TARGET crtsys)
            message(FATAL_ERROR "crtsys target was not found. Add crtsys with CPMAddPackage or use an unpacked crtsys native release bundle.")
        endif()

        target_link_libraries(${_target} crtsys)
        # A source-tree consumer needs the same libcntpr duplicate-symbol
        # policy as a prebuilt-package consumer. Link options placed on the
        # static crtsys archive are not applied by every WDK generator path.
        if(CRTSYS_USE_LIBCNTPR)
            target_link_options(${_target} PRIVATE "/FORCE:MULTIPLE")
        endif()
        if(NOT TARGET WDK::WDMSEC)
            message(FATAL_ERROR "WDK::WDMSEC is required for secure NTL control devices.")
        endif()
        target_link_libraries(${_target} WDK::WDMSEC)
    endif()

    # NTL's owning kernel-network facades use WSK and the standard NPI WSK
    # interface identifier. Keep these system dependencies on the driver
    # model itself so a consumer does not have to rediscover netio.lib and
    # uuid.lib after including ntl/net/kernel/wsk_transport.
    if(NOT TARGET WDK::NETIO)
        message(FATAL_ERROR "WDK::NETIO is required for NTL kernel networking.")
    endif()
    target_link_libraries(${_target} WDK::NETIO uuid.lib)
endfunction()
