cmake_minimum_required(VERSION 3.14 FATAL_ERROR)

project(crtsys_kmdf_reference LANGUAGES C CXX)

include("${CMAKE_CURRENT_LIST_DIR}/../../../cmake/CPM.cmake")

CPMAddPackage(NAME crtsys SOURCE_DIR "${CMAKE_CURRENT_LIST_DIR}/../../..")
include("${crtsys_SOURCE_DIR}/cmake/CrtSys.cmake")

crtsys_add_driver(crtsys_kmdf_reference
  KMDF 1.15
  NTL
  driver/main.cpp
)

add_executable(crtsys_kmdf_reference_app
  app/main.cpp
)

target_include_directories(crtsys_kmdf_reference
  PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}/shared"
)

target_include_directories(crtsys_kmdf_reference_app
  PRIVATE
    "${crtsys_SOURCE_DIR}/include"
    "${CMAKE_CURRENT_LIST_DIR}/shared"
)

target_link_libraries(crtsys_kmdf_reference_app setupapi)

target_compile_options(crtsys_kmdf_reference
  PRIVATE
    "$<$<COMPILE_LANGUAGE:CXX>:/W4>"
    "$<$<COMPILE_LANGUAGE:CXX>:/WX>"
)

target_compile_options(crtsys_kmdf_reference_app
  PRIVATE
    "$<$<COMPILE_LANGUAGE:CXX>:/W4>"
    "$<$<COMPILE_LANGUAGE:CXX>:/WX>"
    "$<$<AND:$<COMPILE_LANGUAGE:CXX>,$<CONFIG:Debug>>:/MTd>"
    "$<$<AND:$<COMPILE_LANGUAGE:CXX>,$<CONFIG:Release>>:/MT>"
)

add_custom_command(TARGET crtsys_kmdf_reference POST_BUILD
  COMMAND "${CMAKE_COMMAND}" -E copy_if_different
          "${CMAKE_CURRENT_LIST_DIR}/crtsys_kmdf_reference.inf"
          "$<TARGET_FILE_DIR:crtsys_kmdf_reference>"
)

include("${crtsys_SOURCE_DIR}/cmake/std-cxx.cmake")
set_std_cxx_latest(crtsys_kmdf_reference)
set_std_cxx_latest(crtsys_kmdf_reference_app)
