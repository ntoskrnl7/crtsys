cmake_minimum_required(VERSION 3.14 FATAL_ERROR)

if(POLICY CMP0091)
  cmake_policy(SET CMP0091 NEW)
endif()

project(crtsys_ntl_net_kernel_contracts LANGUAGES C CXX)

include("${CMAKE_CURRENT_LIST_DIR}/../../../cmake/CPM.cmake")
CPMAddPackage(NAME crtsys SOURCE_DIR "${CMAKE_CURRENT_LIST_DIR}/../../..")
include("${crtsys_SOURCE_DIR}/cmake/CrtSys.cmake")

crtsys_add_driver(crtsys_ntl_net_kernel_contract_driver
  driver/main.cpp
  KERNEL_MSQUIC
  KERNEL_CONTENT_CODECS
  WINVER 0x0A00)
add_executable(crtsys_ntl_net_kernel_contract_app app/main.cpp)

target_include_directories(crtsys_ntl_net_kernel_contract_driver
  PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}/shared")
target_include_directories(crtsys_ntl_net_kernel_contract_app
  PRIVATE
    "${crtsys_SOURCE_DIR}/include"
    "${crtsys_SOURCE_DIR}/examples/wfp/shared"
    "${CMAKE_CURRENT_LIST_DIR}/shared")
if(WDK_PLATFORM MATCHES "^ARM")
  set(_crtsys_ntl_net_contract_ndis_version NDIS630)
else()
  set(_crtsys_ntl_net_contract_ndis_version NDIS60)
endif()
target_compile_definitions(crtsys_ntl_net_kernel_contract_driver
  PRIVATE
    ${_crtsys_ntl_net_contract_ndis_version}
    NDIS_SUPPORT_NDIS6)
target_link_libraries(crtsys_ntl_net_kernel_contract_driver
  WDK::KSECDD
  WDK::FWPKCLNT)
target_link_libraries(crtsys_ntl_net_kernel_contract_app
  advapi32.lib
  bcrypt.lib
  crypt32.lib
  ncrypt.lib
  secur32.lib
  ws2_32.lib)

foreach(target
    crtsys_ntl_net_kernel_contract_driver
    crtsys_ntl_net_kernel_contract_app)
  target_compile_options(${target} PRIVATE /W4 /WX)
endforeach()
target_compile_options(crtsys_ntl_net_kernel_contract_app PRIVATE /EHsc)
set_property(TARGET crtsys_ntl_net_kernel_contract_app PROPERTY
  MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
if(NOT POLICY CMP0091)
  target_compile_options(crtsys_ntl_net_kernel_contract_app PRIVATE
    "/MT$<$<CONFIG:Debug>:d>")
endif()

include("${crtsys_SOURCE_DIR}/cmake/std-cxx.cmake")
set_std_cxx_latest(crtsys_ntl_net_kernel_contract_driver)
set_std_cxx_latest(crtsys_ntl_net_kernel_contract_app)
