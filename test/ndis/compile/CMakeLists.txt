cmake_minimum_required(VERSION 3.14 FATAL_ERROR)

project(crtsys_ndis_compile_contracts LANGUAGES C CXX)

include("${CMAKE_CURRENT_LIST_DIR}/../../../cmake/CPM.cmake")

set(CRTSYS_NTL_MAIN ON)
CPMAddPackage(NAME crtsys SOURCE_DIR "${CMAKE_CURRENT_LIST_DIR}/../../..")
include("${crtsys_SOURCE_DIR}/cmake/CrtSys.cmake")

crtsys_add_driver(crtsys_ndis_compile_contracts
  kernel.cpp
)
target_link_libraries(crtsys_ndis_compile_contracts
  WDK::NDIS
)
target_compile_definitions(crtsys_ndis_compile_contracts
  PRIVATE
    NDIS630=1
    NDIS_WDM=1
    NDISLWF=1
)
target_compile_options(crtsys_ndis_compile_contracts
  PRIVATE
    "$<$<COMPILE_LANGUAGE:CXX>:/W4>"
    "$<$<COMPILE_LANGUAGE:CXX>:/WX>"
)

include("${crtsys_SOURCE_DIR}/cmake/std-cxx.cmake")
set_std_cxx_latest(crtsys_ndis_compile_contracts)
