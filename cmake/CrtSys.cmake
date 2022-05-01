# INCLUDE_DIRECTORIES $(VC_IncludePath);$(WindowsSDK_IncludePath)
cmake_policy(SET CMP0021 OLD)

set(CMAKE_CXX_STANDARD_LIBRARIES " ")
set(CMAKE_C_STANDARD_LIBRARIES ${CMAKE_CXX_STANDARD_LIBRARIES})

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

function(crtsys_add_driver _target)
    cmake_parse_arguments(CRTSYS "" "WINVER" "" ${ARGN})

    EXTENDED_CPP_FEATURES_ON(${_target})
    add_executable(${_target} ${CRTSYS_UNPARSED_ARGUMENTS})
    
    set_target_properties(${_target} PROPERTIES SUFFIX ".sys")
    set_target_properties(${_target} PROPERTIES COMPILE_OPTIONS "${WDK_COMPILE_FLAGS}")
    set_target_properties(${_target} PROPERTIES COMPILE_DEFINITIONS
        "${WDK_COMPILE_DEFINITIONS};$<$<CONFIG:Debug>:${WDK_COMPILE_DEFINITIONS_DEBUG}>;_WIN32_WINNT=${WDK_WINVER}"
        )
    set_target_properties(${_target} PROPERTIES LINK_FLAGS "${WDK_LINK_FLAGS}")

    set_property(TARGET crtsys_test PROPERTY INCLUDE_DIRECTORIES "${crtsys_SOURCE_DIR}/include;${crtsys_SOURCE_DIR}/include/$(VCToolsVersion);${crtsys_SOURCE_DIR}/include/${MSVC_TOOLSET_VERSION};${crtsys_SOURCE_DIR}/include/$(VCToolsVersion)/stl;${crtsys_SOURCE_DIR}/include/${MSVC_TOOLSET_VERSION}/stl;$(VC_IncludePath);$(WindowsSDK_IncludePath)")
    target_include_directories(${_target} SYSTEM PRIVATE
        "${WDK_ROOT}/Include/${WDK_VERSION}/shared"
        "${WDK_ROOT}/Include/${WDK_VERSION}/km"
        "${WDK_ROOT}/Include/${WDK_VERSION}/km/crt"
        )
    
    target_compile_definitions(${_target} PRIVATE "_NO_CRT_STDIO_INLINE=0")

    if(CRTSYS_NTL_MAIN)
      target_compile_definitions(crtsys PUBLIC CRTSYS_USE_NTL_MAIN)
    endif()
    target_link_libraries(${_target} crtsys WDK::NTOSKRNL WDK::HAL WDK::WMILIB WDK::BUFFEROVERFLOWK)

    if(CMAKE_SIZEOF_VOID_P EQUAL 4)
        target_link_libraries(${_target} WDK::MEMCMP)
    endif()

    if(CMAKE_SIZEOF_VOID_P EQUAL 4)
        set_property(TARGET ${_target} APPEND_STRING PROPERTY LINK_FLAGS "/ENTRY:CrtSysDriverEntry@8")
    elseif(CMAKE_SIZEOF_VOID_P  EQUAL 8)
        set_property(TARGET ${_target} APPEND_STRING PROPERTY LINK_FLAGS "/ENTRY:CrtSysDriverEntry")
    endif()
endfunction()
