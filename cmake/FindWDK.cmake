# crtsys wrapper around the upstream FindWDK module.
#
# The upstream module picks the newest installed WDK header tree. GitHub's
# windows-2022 image can have a newer SDK/WDK installed whose x86 kernel libs
# are absent or incomplete. Keep the upstream commands, but fill missing WDK::
# imported library targets from an installed kernel library directory for the
# current platform. If an explicitly requested WDK version has both headers and
# libraries, use it; otherwise keep the discovered headers and only fall back
# for missing library targets.

if(NOT DEFINED FindWDK_SOURCE_DIR)
    message(FATAL_ERROR "FindWDK_SOURCE_DIR is not defined. Add the upstream FindWDK package before find_package(WDK).")
endif()

set(_CRTSYS_UPSTREAM_FINDWDK "${FindWDK_SOURCE_DIR}/cmake/FindWdk.cmake")
if(NOT EXISTS "${_CRTSYS_UPSTREAM_FINDWDK}")
    message(FATAL_ERROR "Upstream FindWDK module was not found: ${_CRTSYS_UPSTREAM_FINDWDK}")
endif()

include("${_CRTSYS_UPSTREAM_FINDWDK}")

function(crtsys_import_wdk_libraries _version _out_imported)
    set(_CRTSYS_WDK_LIB_DIR "${WDK_ROOT}/Lib/${_version}/km/${WDK_PLATFORM}")
    file(GLOB _CRTSYS_WDK_LIBRARIES "${_CRTSYS_WDK_LIB_DIR}/*.lib")
    if(NOT _CRTSYS_WDK_LIBRARIES)
        set(${_out_imported} FALSE PARENT_SCOPE)
        return()
    endif()

    foreach(_CRTSYS_WDK_LIBRARY IN LISTS _CRTSYS_WDK_LIBRARIES)
        get_filename_component(_CRTSYS_WDK_LIBRARY_NAME "${_CRTSYS_WDK_LIBRARY}" NAME_WE)
        string(TOUPPER "${_CRTSYS_WDK_LIBRARY_NAME}" _CRTSYS_WDK_LIBRARY_NAME)

        if(NOT TARGET WDK::${_CRTSYS_WDK_LIBRARY_NAME})
            add_library(WDK::${_CRTSYS_WDK_LIBRARY_NAME} INTERFACE IMPORTED)
        endif()

        set_property(
            TARGET WDK::${_CRTSYS_WDK_LIBRARY_NAME}
            PROPERTY INTERFACE_LINK_LIBRARIES "${_CRTSYS_WDK_LIBRARY}"
        )
    endforeach()

    set(${_out_imported} TRUE PARENT_SCOPE)
endfunction()

set(_CRTSYS_REQUESTED_WDK_VERSION)
foreach(_CRTSYS_WDK_VERSION_CANDIDATE IN ITEMS
        "${CRTSYS_WDK_VERSION}"
        "${LDK_WDK_VERSION}"
        "${CMAKE_VS_WINDOWS_TARGET_PLATFORM_VERSION}"
        "${CMAKE_SYSTEM_VERSION}")
    if(_CRTSYS_WDK_VERSION_CANDIDATE MATCHES "^[0-9]+\\.[0-9]+\\.[0-9]+\\.[0-9]+$")
        set(_CRTSYS_REQUESTED_WDK_VERSION "${_CRTSYS_WDK_VERSION_CANDIDATE}")
        break()
    endif()
endforeach()

if(_CRTSYS_REQUESTED_WDK_VERSION)
    set(_CRTSYS_REQUESTED_WDK_INCLUDE "${WDK_ROOT}/Include/${_CRTSYS_REQUESTED_WDK_VERSION}/km/ntddk.h")
    crtsys_import_wdk_libraries("${_CRTSYS_REQUESTED_WDK_VERSION}" _CRTSYS_IMPORTED_REQUESTED_WDK_LIBRARIES)

    if(EXISTS "${_CRTSYS_REQUESTED_WDK_INCLUDE}")
        if(_CRTSYS_IMPORTED_REQUESTED_WDK_LIBRARIES AND NOT "${WDK_VERSION}" STREQUAL "${_CRTSYS_REQUESTED_WDK_VERSION}")
            message(STATUS "Using requested WDK ${_CRTSYS_REQUESTED_WDK_VERSION} headers and libraries instead of discovered WDK ${WDK_VERSION}")
            set(WDK_VERSION "${_CRTSYS_REQUESTED_WDK_VERSION}")
            set(WINSDK "${WDK_ROOT}/bin/${WDK_VERSION}/${WDK_PLATFORM}")
            set(MAKECERT "${WINSDK}/makecert.exe")
            set(CERT2SPC "${WINSDK}/cert2spc.exe")
            set(PVK2PFX "${WINSDK}/pvk2pfx.exe")
            set(SIGNTOOL "${WINSDK}/signtool.exe")
        elseif(NOT _CRTSYS_IMPORTED_REQUESTED_WDK_LIBRARIES)
            message(STATUS "Requested WDK ${_CRTSYS_REQUESTED_WDK_VERSION} libraries were not found for ${WDK_PLATFORM}; keeping discovered WDK ${WDK_VERSION} libraries")
        endif()
    elseif(_CRTSYS_IMPORTED_REQUESTED_WDK_LIBRARIES)
        message(STATUS "Using requested WDK ${_CRTSYS_REQUESTED_WDK_VERSION} libraries with discovered WDK ${WDK_VERSION} headers")
    elseif(NOT "${WDK_VERSION}" STREQUAL "${_CRTSYS_REQUESTED_WDK_VERSION}")
        message(STATUS "Requested WDK ${_CRTSYS_REQUESTED_WDK_VERSION} headers and libraries were not found for ${WDK_PLATFORM}; keeping discovered WDK ${WDK_VERSION}")
    endif()
endif()

if(NOT TARGET WDK::NTOSKRNL)
    file(GLOB _CRTSYS_INSTALLED_WDK_LIB_DIRS LIST_DIRECTORIES true "${WDK_ROOT}/Lib/*/km/${WDK_PLATFORM}")
    set(_CRTSYS_INSTALLED_WDK_LIB_VERSIONS)
    foreach(_CRTSYS_INSTALLED_WDK_LIB_DIR IN LISTS _CRTSYS_INSTALLED_WDK_LIB_DIRS)
        get_filename_component(_CRTSYS_WDK_KM_DIR "${_CRTSYS_INSTALLED_WDK_LIB_DIR}" DIRECTORY)
        get_filename_component(_CRTSYS_WDK_VERSION_DIR "${_CRTSYS_WDK_KM_DIR}" DIRECTORY)
        get_filename_component(_CRTSYS_INSTALLED_WDK_LIB_VERSION "${_CRTSYS_WDK_VERSION_DIR}" NAME)
        list(APPEND _CRTSYS_INSTALLED_WDK_LIB_VERSIONS "${_CRTSYS_INSTALLED_WDK_LIB_VERSION}")
    endforeach()

    list(REMOVE_DUPLICATES _CRTSYS_INSTALLED_WDK_LIB_VERSIONS)
    list(SORT _CRTSYS_INSTALLED_WDK_LIB_VERSIONS)
    list(REVERSE _CRTSYS_INSTALLED_WDK_LIB_VERSIONS)

    foreach(_CRTSYS_INSTALLED_WDK_LIB_VERSION IN LISTS _CRTSYS_INSTALLED_WDK_LIB_VERSIONS)
        crtsys_import_wdk_libraries("${_CRTSYS_INSTALLED_WDK_LIB_VERSION}" _CRTSYS_IMPORTED_FALLBACK_WDK_LIBRARIES)
        if(_CRTSYS_IMPORTED_FALLBACK_WDK_LIBRARIES)
            message(STATUS "Using WDK ${_CRTSYS_INSTALLED_WDK_LIB_VERSION} libraries with discovered WDK ${WDK_VERSION} headers for ${WDK_PLATFORM}")
            break()
        endif()
    endforeach()
endif()
