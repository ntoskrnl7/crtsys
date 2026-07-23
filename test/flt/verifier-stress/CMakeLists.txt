cmake_minimum_required(VERSION 3.14 FATAL_ERROR)

project(crtsys_flt_verifier_stress LANGUAGES CXX)

include("${CMAKE_CURRENT_LIST_DIR}/../../../cmake/CPM.cmake")

CPMAddPackage(NAME crtsys SOURCE_DIR "${CMAKE_CURRENT_LIST_DIR}/../../..")
include("${crtsys_SOURCE_DIR}/cmake/CrtSys.cmake")

add_executable(crtsys_flt_verifier_stress_app
  app/main.cpp
)

target_include_directories(crtsys_flt_verifier_stress_app
  PRIVATE
    "${crtsys_SOURCE_DIR}/include"
)

target_link_libraries(crtsys_flt_verifier_stress_app PRIVATE FltLib.lib
                                                     Advapi32.lib)

target_compile_options(crtsys_flt_verifier_stress_app
  PRIVATE
    /W4
    /WX
    /EHsc
    "$<$<CONFIG:Debug>:/MTd>"
    "$<$<CONFIG:Release>:/MT>"
)

include("${crtsys_SOURCE_DIR}/cmake/std-cxx.cmake")
set_std_cxx_latest(crtsys_flt_verifier_stress_app)
