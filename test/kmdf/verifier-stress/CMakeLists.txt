cmake_minimum_required(VERSION 3.14 FATAL_ERROR)

project(crtsys_kmdf_verifier_stress LANGUAGES C CXX)

include("${CMAKE_CURRENT_LIST_DIR}/../../../cmake/CPM.cmake")

CPMAddPackage(NAME crtsys SOURCE_DIR "${CMAKE_CURRENT_LIST_DIR}/../../..")
include("${crtsys_SOURCE_DIR}/cmake/CrtSys.cmake")

crtsys_add_driver(crtsys_kmdf_verifier_stress
  KMDF 1.15
  NTL
  driver/main.cpp
)

add_executable(crtsys_kmdf_verifier_stress_app
  app/main.cpp
)

target_include_directories(crtsys_kmdf_verifier_stress
  PRIVATE "${CMAKE_CURRENT_LIST_DIR}/shared"
)

target_include_directories(crtsys_kmdf_verifier_stress_app
  PRIVATE
    "${crtsys_SOURCE_DIR}/include"
    "${CMAKE_CURRENT_LIST_DIR}/shared"
)

target_link_libraries(crtsys_kmdf_verifier_stress WDK::WDMSEC)

target_compile_options(crtsys_kmdf_verifier_stress
  PRIVATE
    "$<$<COMPILE_LANGUAGE:CXX>:/W4>"
    "$<$<COMPILE_LANGUAGE:CXX>:/WX>"
)

target_compile_options(crtsys_kmdf_verifier_stress_app
  PRIVATE
    "$<$<COMPILE_LANGUAGE:CXX>:/W4>"
    "$<$<COMPILE_LANGUAGE:CXX>:/WX>"
    "$<$<AND:$<COMPILE_LANGUAGE:CXX>,$<CONFIG:Debug>>:/MTd>"
    "$<$<AND:$<COMPILE_LANGUAGE:CXX>,$<CONFIG:Release>>:/MT>"
)

include("${crtsys_SOURCE_DIR}/cmake/std-cxx.cmake")
set_std_cxx_latest(crtsys_kmdf_verifier_stress)
set_std_cxx_latest(crtsys_kmdf_verifier_stress_app)
