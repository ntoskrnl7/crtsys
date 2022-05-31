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
    cmake_parse_arguments(WDK "" "WINVER" "" ${ARGN})
    wdk_add_driver(${_target} ${WDK_UNPARSED_ARGUMENTS} CUSTOM_ENTRY_POINT CrtSysDriverEntry EXTENDED_CPP_FEATURES)

    target_link_libraries(${_target} crtsys)

    get_target_property(INC_DIR_TMP ${_target} INCLUDE_DIRECTORIES)
    set_property(TARGET ${_target} PROPERTY INCLUDE_DIRECTORIES "${crtsys_SOURCE_DIR}/include;${crtsys_SOURCE_DIR}/include/.internal/msvc/$(VCToolsVersion);${crtsys_SOURCE_DIR}/include/.internal/msvc/${MSVC_TOOLSET_VERSION};${crtsys_SOURCE_DIR}/include/.internal/msvc/$(VCToolsVersion)/stl;${crtsys_SOURCE_DIR}/include/.internal/msvc/${MSVC_TOOLSET_VERSION}/stl;$(VC_IncludePath);$(WindowsSDK_IncludePath);${INC_DIR_TMP}")

    if(EXISTS "${crtsys_SOURCE_DIR}/include/.internal/winsdk/${CMAKE_VS_WINDOWS_TARGET_PLATFORM_VERSION}/wdk/${WDK_VERSION}/forced.h")
      target_compile_options(${_target} PRIVATE /FI"${crtsys_SOURCE_DIR}/include/.internal/winsdk/${CMAKE_VS_WINDOWS_TARGET_PLATFORM_VERSION}/wdk/${WDK_VERSION}/forced.h")
    endif()

    target_compile_options(${_target} PRIVATE /FI"${crtsys_SOURCE_DIR}/include/.internal/adjust_link_order")

    if(CRTSYS_NTL_MAIN)
      target_compile_definitions(${_target} PUBLIC CRTSYS_USE_NTL_MAIN)
    endif()
endfunction()
