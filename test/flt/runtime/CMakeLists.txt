cmake_minimum_required(VERSION 3.14 FATAL_ERROR)

project(crtsys_flt_runtime_test LANGUAGES C CXX)

include("${CMAKE_CURRENT_LIST_DIR}/../../../cmake/CPM.cmake")

CPMAddPackage(NAME crtsys SOURCE_DIR "${CMAKE_CURRENT_LIST_DIR}/../../..")
include("${crtsys_SOURCE_DIR}/cmake/CrtSys.cmake")

crtsys_add_driver(crtsys_flt_runtime_test
  MINIFILTER
  NTL
  driver/communication_advanced.cpp
  driver/main.cpp
)

add_executable(crtsys_flt_runtime_test_app
  app/communication_advanced.cpp
  app/coroutine_runtime.cpp
  app/main.cpp
)

target_include_directories(crtsys_flt_runtime_test_app
  PRIVATE
    "${crtsys_SOURCE_DIR}/include"
    "${CMAKE_CURRENT_LIST_DIR}/shared"
)

target_link_libraries(crtsys_flt_runtime_test_app PRIVATE FltLib.lib)

target_compile_options(crtsys_flt_runtime_test
  PRIVATE
    "$<$<COMPILE_LANGUAGE:CXX>:/W4>"
    "$<$<COMPILE_LANGUAGE:CXX>:/WX>"
)

target_compile_options(crtsys_flt_runtime_test_app
  PRIVATE
    "$<$<COMPILE_LANGUAGE:CXX>:/W4>"
    "$<$<COMPILE_LANGUAGE:CXX>:/WX>"
    "$<$<COMPILE_LANGUAGE:CXX>:/EHsc>"
    "$<$<AND:$<COMPILE_LANGUAGE:CXX>,$<CONFIG:Debug>>:/MTd>"
    "$<$<AND:$<COMPILE_LANGUAGE:CXX>,$<CONFIG:Release>>:/MT>"
)

include("${crtsys_SOURCE_DIR}/cmake/std-cxx.cmake")
set_std_cxx_latest(crtsys_flt_runtime_test)
set_std_cxx_latest(crtsys_flt_runtime_test_app)
