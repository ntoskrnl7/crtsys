cmake_minimum_required(VERSION 3.14 FATAL_ERROR)

project(crtsys_kmdf_compile_contracts LANGUAGES C CXX)

include("${CMAKE_CURRENT_LIST_DIR}/../../../cmake/CPM.cmake")

CPMAddPackage(NAME crtsys SOURCE_DIR "${CMAKE_CURRENT_LIST_DIR}/../../..")
include("${crtsys_SOURCE_DIR}/cmake/CrtSys.cmake")

crtsys_add_driver(crtsys_kmdf_compile_contracts
  KMDF 1.15
  NTL
  main.cpp
  ownership.cpp
  callbacks.cpp
  filter_and_pnp.cpp
  hardware.cpp
  wmi.cpp
)

target_compile_options(crtsys_kmdf_compile_contracts
  PRIVATE
    "$<$<COMPILE_LANGUAGE:CXX>:/W4>"
    "$<$<COMPILE_LANGUAGE:CXX>:/WX>"
)

include("${crtsys_SOURCE_DIR}/cmake/std-cxx.cmake")
set_std_cxx_latest(crtsys_kmdf_compile_contracts)
