cmake_minimum_required(VERSION 3.14 FATAL_ERROR)

project(crtsys_kmdf_filter_stack_ntl_sample LANGUAGES C CXX)

include("${CMAKE_CURRENT_LIST_DIR}/../../../cmake/CPM.cmake")

CPMAddPackage(NAME crtsys SOURCE_DIR "${CMAKE_CURRENT_LIST_DIR}/../../..")
include("${crtsys_SOURCE_DIR}/cmake/CrtSys.cmake")

crtsys_add_driver(crtsys_kmdf_filter_stack_target
  KMDF 1.15
  NTL
  target/main.cpp
)

crtsys_add_driver(crtsys_kmdf_filter_stack_filter
  KMDF 1.15
  NTL
  filter/main.cpp
)

add_executable(crtsys_kmdf_filter_stack_app
  app/main.cpp
)

foreach(target IN ITEMS
    crtsys_kmdf_filter_stack_target
    crtsys_kmdf_filter_stack_filter)
  target_include_directories(${target}
    PRIVATE
      "${CMAKE_CURRENT_LIST_DIR}/shared"
  )
  target_compile_options(${target}
    PRIVATE
      "$<$<COMPILE_LANGUAGE:CXX>:/W4>"
      "$<$<COMPILE_LANGUAGE:CXX>:/WX>"
  )
endforeach()

target_include_directories(crtsys_kmdf_filter_stack_app
  PRIVATE
    "${crtsys_SOURCE_DIR}/include"
    "${CMAKE_CURRENT_LIST_DIR}/shared"
)

target_link_libraries(crtsys_kmdf_filter_stack_app setupapi)

target_compile_options(crtsys_kmdf_filter_stack_app
  PRIVATE
    "$<$<COMPILE_LANGUAGE:CXX>:/W4>"
    "$<$<COMPILE_LANGUAGE:CXX>:/WX>"
    "$<$<AND:$<COMPILE_LANGUAGE:CXX>,$<CONFIG:Debug>>:/MTd>"
    "$<$<AND:$<COMPILE_LANGUAGE:CXX>,$<CONFIG:Release>>:/MT>"
)

add_custom_command(TARGET crtsys_kmdf_filter_stack_target POST_BUILD
  COMMAND "${CMAKE_COMMAND}" -E copy_if_different
          "${CMAKE_CURRENT_LIST_DIR}/crtsys_kmdf_filter_stack_sample.inf"
          "$<TARGET_FILE_DIR:crtsys_kmdf_filter_stack_target>"
)

include("${crtsys_SOURCE_DIR}/cmake/std-cxx.cmake")
set_std_cxx_latest(crtsys_kmdf_filter_stack_target)
set_std_cxx_latest(crtsys_kmdf_filter_stack_filter)
set_std_cxx_latest(crtsys_kmdf_filter_stack_app)
