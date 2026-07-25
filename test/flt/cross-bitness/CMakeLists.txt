cmake_minimum_required(VERSION 3.14 FATAL_ERROR)

project(crtsys_flt_runtime_cross_bitness_app LANGUAGES CXX)

if (NOT CMAKE_SIZEOF_VOID_P EQUAL 4)
  message(FATAL_ERROR
    "This fixture must be configured as a 32-bit user-mode application.")
endif()

include("${CMAKE_CURRENT_LIST_DIR}/../../../cmake/CPM.cmake")

CPMAddPackage(NAME crtsys SOURCE_DIR "${CMAKE_CURRENT_LIST_DIR}/../../..")
include("${crtsys_SOURCE_DIR}/cmake/CrtSys.cmake")

include("${CMAKE_CURRENT_LIST_DIR}/../runtime/app/runtime_sources.cmake")
add_executable(crtsys_flt_runtime_x86_app
  ${CRTSYS_FLT_RUNTIME_APP_SOURCES}
)

target_include_directories(crtsys_flt_runtime_x86_app
  PRIVATE
    "${crtsys_SOURCE_DIR}/include"
    "${CMAKE_CURRENT_LIST_DIR}/../runtime/shared"
)

target_link_libraries(crtsys_flt_runtime_x86_app
  PRIVATE ${CRTSYS_FLT_RUNTIME_APP_LIBRARIES}
)

target_compile_options(crtsys_flt_runtime_x86_app
  PRIVATE
    /W4
    /WX
    /EHsc
    "$<$<CONFIG:Debug>:/MTd>"
    "$<$<CONFIG:Release>:/MT>"
)

include("${crtsys_SOURCE_DIR}/cmake/std-cxx.cmake")
set_std_cxx_latest(crtsys_flt_runtime_x86_app)
