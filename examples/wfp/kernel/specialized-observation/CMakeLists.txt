cmake_minimum_required(VERSION 3.14 FATAL_ERROR)

project(crtsys_wfp_specialized_observation LANGUAGES C CXX)

include("${CMAKE_CURRENT_LIST_DIR}/../../../../cmake/CPM.cmake")

CPMAddPackage(NAME crtsys SOURCE_DIR "${CMAKE_CURRENT_LIST_DIR}/../../../..")
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
add_executable(crtsys_wfp_specialized_observation_controller app/main.cpp)
add_executable(crtsys_wfp_specialized_observation_acceptance
  "${crtsys_SOURCE_DIR}/test/wfp/runtime/fixtures/kernel/specialized-observation/main.cpp"
)

target_include_directories(crtsys_wfp_specialized_observation
  PRIVATE "${CMAKE_CURRENT_LIST_DIR}/shared"
)
target_include_directories(crtsys_wfp_specialized_observation_controller
  PRIVATE
    "${crtsys_SOURCE_DIR}/include"
    "${CMAKE_CURRENT_LIST_DIR}/shared"
    "${CMAKE_CURRENT_LIST_DIR}/../../shared"
)
target_include_directories(crtsys_wfp_specialized_observation_acceptance
  PRIVATE
    "${crtsys_SOURCE_DIR}/include"
    "${crtsys_SOURCE_DIR}/test/wfp/runtime/fixtures/common"
    "${CMAKE_CURRENT_LIST_DIR}/shared"
)
target_link_libraries(crtsys_wfp_specialized_observation_controller
  PRIVATE fwpuclnt uuid
)
target_link_libraries(crtsys_wfp_specialized_observation_acceptance
  PRIVATE ws2_32 iphlpapi
)

foreach(target
    crtsys_wfp_specialized_observation
    crtsys_wfp_specialized_observation_controller
    crtsys_wfp_specialized_observation_acceptance)
  target_compile_options(${target}
    PRIVATE
      "$<$<COMPILE_LANGUAGE:CXX>:/W4>"
      "$<$<COMPILE_LANGUAGE:CXX>:/WX>"
  )
endforeach()
foreach(target
    crtsys_wfp_specialized_observation_controller
    crtsys_wfp_specialized_observation_acceptance)
target_compile_options(${target}
  PRIVATE
    "$<$<COMPILE_LANGUAGE:CXX>:/EHsc>"
    "$<$<AND:$<COMPILE_LANGUAGE:CXX>,$<CONFIG:Debug>>:/MTd>"
    "$<$<AND:$<COMPILE_LANGUAGE:CXX>,$<CONFIG:Release>>:/MT>"
)
endforeach()

add_dependencies(crtsys_wfp_specialized_observation_acceptance
  crtsys_wfp_specialized_observation_controller)

add_custom_command(TARGET crtsys_wfp_specialized_observation POST_BUILD
  COMMAND "${CMAKE_COMMAND}" -E copy_if_different
          "${CMAKE_CURRENT_LIST_DIR}/crtsys_wfp_specialized_observation.inf"
          "$<TARGET_FILE_DIR:crtsys_wfp_specialized_observation>"
)

include("${crtsys_SOURCE_DIR}/cmake/std-cxx.cmake")
set_std_cxx_latest(crtsys_wfp_specialized_observation)
set_std_cxx_latest(crtsys_wfp_specialized_observation_controller)
set_std_cxx_latest(crtsys_wfp_specialized_observation_acceptance)
