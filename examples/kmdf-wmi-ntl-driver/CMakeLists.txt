cmake_minimum_required(VERSION 3.14 FATAL_ERROR)

project(crtsys_kmdf_wmi_ntl_sample LANGUAGES C CXX RC)

include("${CMAKE_CURRENT_LIST_DIR}/../../cmake/CPM.cmake")

CPMAddPackage(NAME crtsys SOURCE_DIR "${CMAKE_CURRENT_LIST_DIR}/../..")
include("${crtsys_SOURCE_DIR}/cmake/CrtSys.cmake")

find_program(CRTSYS_MOFCOMP_EXECUTABLE mofcomp.exe
  HINTS "$ENV{WINDIR}/System32/Wbem")
find_program(CRTSYS_WMIMOFCK_EXECUTABLE wmimofck.exe
  HINTS
    "$ENV{ProgramFiles\(x86\)}/Windows Kits/10/bin/${CMAKE_VS_WINDOWS_TARGET_PLATFORM_VERSION}/x64"
    "$ENV{ProgramFiles\(x86\)}/Windows Kits/10/bin/x64")
if(NOT CRTSYS_MOFCOMP_EXECUTABLE OR NOT CRTSYS_WMIMOFCK_EXECUTABLE)
  message(FATAL_ERROR "The WDK MOF compiler tools were not found.")
endif()

set(_wmi_mof "${CMAKE_CURRENT_LIST_DIR}/crtsys_kmdf_wmi_ntl_sample.mof")
set(_wmi_bmf "${CMAKE_CURRENT_BINARY_DIR}/crtsys_kmdf_wmi_ntl_sample.bmf")
set(_wmi_rc "${CMAKE_CURRENT_BINARY_DIR}/crtsys_kmdf_wmi_ntl_sample.rc")

add_custom_command(
  OUTPUT "${_wmi_bmf}"
  COMMAND "${CRTSYS_MOFCOMP_EXECUTABLE}" "-B:${_wmi_bmf}" "${_wmi_mof}"
  COMMAND "${CRTSYS_WMIMOFCK_EXECUTABLE}" "${_wmi_bmf}"
  DEPENDS "${_wmi_mof}"
  VERBATIM
)
add_custom_target(crtsys_kmdf_wmi_ntl_sample_mof DEPENDS "${_wmi_bmf}")

file(GENERATE OUTPUT "${_wmi_rc}" CONTENT
  "#include <windows.h>\nCrtSysKmdfWmi MOFDATA \"${_wmi_bmf}\"\n")

crtsys_add_driver(crtsys_kmdf_wmi_ntl_sample
  KMDF 1.15
  NTL
  driver/main.cpp
  "${_wmi_rc}"
)
add_dependencies(crtsys_kmdf_wmi_ntl_sample
  crtsys_kmdf_wmi_ntl_sample_mof)

add_executable(crtsys_kmdf_wmi_ntl_sample_app app/main.cpp)

target_include_directories(crtsys_kmdf_wmi_ntl_sample PRIVATE shared)
target_include_directories(crtsys_kmdf_wmi_ntl_sample_app PRIVATE
  "${crtsys_SOURCE_DIR}/include"
  shared
)
target_link_libraries(crtsys_kmdf_wmi_ntl_sample_app
  setupapi wbemuuid ole32 oleaut32)

target_compile_options(crtsys_kmdf_wmi_ntl_sample PRIVATE
  "$<$<COMPILE_LANGUAGE:CXX>:/W4>"
  "$<$<COMPILE_LANGUAGE:CXX>:/WX>"
)
target_compile_options(crtsys_kmdf_wmi_ntl_sample_app PRIVATE
  "$<$<COMPILE_LANGUAGE:CXX>:/W4>"
  "$<$<COMPILE_LANGUAGE:CXX>:/WX>"
  "$<$<AND:$<COMPILE_LANGUAGE:CXX>,$<CONFIG:Debug>>:/MTd>"
  "$<$<AND:$<COMPILE_LANGUAGE:CXX>,$<CONFIG:Release>>:/MT>"
)

add_custom_command(TARGET crtsys_kmdf_wmi_ntl_sample POST_BUILD
  COMMAND "${CMAKE_COMMAND}" -E copy_if_different
          "${CMAKE_CURRENT_LIST_DIR}/crtsys_kmdf_wmi_ntl_sample.inf"
          "$<TARGET_FILE_DIR:crtsys_kmdf_wmi_ntl_sample>"
)

include("${crtsys_SOURCE_DIR}/cmake/std-cxx.cmake")
set_std_cxx_latest(crtsys_kmdf_wmi_ntl_sample)
set_std_cxx_latest(crtsys_kmdf_wmi_ntl_sample_app)
