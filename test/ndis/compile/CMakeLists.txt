cmake_minimum_required(VERSION 3.14 FATAL_ERROR)

project(crtsys_ndis_compile_contracts LANGUAGES C CXX)

include("${CMAKE_CURRENT_LIST_DIR}/../../../cmake/CPM.cmake")

set(CRTSYS_NTL_MAIN OFF)
CPMAddPackage(NAME crtsys SOURCE_DIR "${CMAKE_CURRENT_LIST_DIR}/../../..")
include("${crtsys_SOURCE_DIR}/cmake/CrtSys.cmake")

crtsys_add_driver(crtsys_ndis_compile_contracts NDIS NTL
  kernel.cpp
)
target_compile_options(crtsys_ndis_compile_contracts
  PRIVATE
    "$<$<COMPILE_LANGUAGE:CXX>:/W4>"
    "$<$<COMPILE_LANGUAGE:CXX>:/WX>"
)

include("${crtsys_SOURCE_DIR}/cmake/std-cxx.cmake")
set_std_cxx_latest(crtsys_ndis_compile_contracts)
