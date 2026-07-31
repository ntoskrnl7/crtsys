cmake_minimum_required(VERSION 3.14 FATAL_ERROR)

project(crtsys_wfp_specialized_observation LANGUAGES C CXX)

include("${CMAKE_CURRENT_LIST_DIR}/../../../cmake/CPM.cmake")

CPMAddPackage(NAME crtsys SOURCE_DIR "${CMAKE_CURRENT_LIST_DIR}/../../..")
include("${crtsys_SOURCE_DIR}/cmake/CrtSys.cmake")

crtsys_add_driver(crtsys_wfp_specialized_observation
  WFP
  NTL
  driver/main.cpp
)
# Fast-transport layers were introduced in Windows 8.1. The general WFP
# driver model keeps the repository's Windows 8 baseline; this fixture opts
# into the newer contract explicitly instead of silently raising every
# consumer's minimum OS.
get_target_property(
  specialized_observation_definitions
  crtsys_wfp_specialized_observation
  COMPILE_DEFINITIONS
)
list(
  REMOVE_ITEM specialized_observation_definitions
  "NTDDI_VERSION=NTDDI_WIN8"
)
list(
  APPEND specialized_observation_definitions
  "NTDDI_VERSION=NTDDI_WINBLUE"
)
set_target_properties(
  crtsys_wfp_specialized_observation
  PROPERTIES COMPILE_DEFINITIONS "${specialized_observation_definitions}"
)
add_executable(crtsys_wfp_specialized_observation_app app/main.cpp)

target_include_directories(crtsys_wfp_specialized_observation
  PRIVATE "${CMAKE_CURRENT_LIST_DIR}/shared"
)
target_include_directories(crtsys_wfp_specialized_observation_app
  PRIVATE
    "${crtsys_SOURCE_DIR}/include"
    "${CMAKE_CURRENT_LIST_DIR}/shared"
)
target_link_libraries(crtsys_wfp_specialized_observation_app
  PRIVATE fwpuclnt ws2_32
)

foreach(target
    crtsys_wfp_specialized_observation
    crtsys_wfp_specialized_observation_app)
  target_compile_options(${target}
    PRIVATE
      "$<$<COMPILE_LANGUAGE:CXX>:/W4>"
      "$<$<COMPILE_LANGUAGE:CXX>:/WX>"
  )
endforeach()
target_compile_options(crtsys_wfp_specialized_observation_app
  PRIVATE
    "$<$<COMPILE_LANGUAGE:CXX>:/EHsc>"
    "$<$<AND:$<COMPILE_LANGUAGE:CXX>,$<CONFIG:Debug>>:/MTd>"
    "$<$<AND:$<COMPILE_LANGUAGE:CXX>,$<CONFIG:Release>>:/MT>"
)

add_custom_command(TARGET crtsys_wfp_specialized_observation POST_BUILD
  COMMAND "${CMAKE_COMMAND}" -E copy_if_different
          "${CMAKE_CURRENT_LIST_DIR}/crtsys_wfp_specialized_observation.inf"
          "$<TARGET_FILE_DIR:crtsys_wfp_specialized_observation>"
)

include("${crtsys_SOURCE_DIR}/cmake/std-cxx.cmake")
set_std_cxx_latest(crtsys_wfp_specialized_observation)
set_std_cxx_latest(crtsys_wfp_specialized_observation_app)
