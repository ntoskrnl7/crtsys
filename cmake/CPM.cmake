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
