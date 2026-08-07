cmake_minimum_required(VERSION 3.14 FATAL_ERROR)

project(crtsys_wfp_bind_redirect LANGUAGES C CXX)

include("${CMAKE_CURRENT_LIST_DIR}/../../../../cmake/CPM.cmake")

CPMAddPackage(NAME crtsys SOURCE_DIR "${CMAKE_CURRENT_LIST_DIR}/../../../..")
include("${crtsys_SOURCE_DIR}/cmake/CrtSys.cmake")

crtsys_add_driver(crtsys_wfp_bind_redirect
  WFP
  NTL
  driver/main.cpp
)
add_executable(crtsys_wfp_bind_redirect_controller app/main.cpp)
add_executable(crtsys_wfp_bind_redirect_acceptance
  "${crtsys_SOURCE_DIR}/test/wfp/runtime/fixtures/kernel/bind-redirect/main.cpp"
)
add_dependencies(crtsys_wfp_bind_redirect_controller
  crtsys_wfp_bind_redirect)
add_dependencies(crtsys_wfp_bind_redirect_acceptance
  crtsys_wfp_bind_redirect_controller)

target_include_directories(crtsys_wfp_bind_redirect
  PRIVATE "${CMAKE_CURRENT_LIST_DIR}/shared"
)
target_include_directories(crtsys_wfp_bind_redirect_controller
  PRIVATE
    "${crtsys_SOURCE_DIR}/include"
    "${CMAKE_CURRENT_LIST_DIR}/shared"
)
target_include_directories(crtsys_wfp_bind_redirect_acceptance
  PRIVATE
    "${crtsys_SOURCE_DIR}/include"
    "${CMAKE_CURRENT_LIST_DIR}/shared"
)
target_link_libraries(crtsys_wfp_bind_redirect WDK::NDIS)
target_link_libraries(crtsys_wfp_bind_redirect_controller
  PRIVATE fwpuclnt uuid
)
target_link_libraries(crtsys_wfp_bind_redirect_acceptance
  PRIVATE ws2_32
)

foreach(target
    crtsys_wfp_bind_redirect
    crtsys_wfp_bind_redirect_controller
    crtsys_wfp_bind_redirect_acceptance)
  target_compile_options(${target}
    PRIVATE
      "$<$<COMPILE_LANGUAGE:CXX>:/W4>"
      "$<$<COMPILE_LANGUAGE:CXX>:/WX>"
  )
endforeach()
foreach(target
    crtsys_wfp_bind_redirect_controller
    crtsys_wfp_bind_redirect_acceptance)
  target_compile_options(${target}
    PRIVATE
      "$<$<COMPILE_LANGUAGE:CXX>:/EHsc>"
      "$<$<AND:$<COMPILE_LANGUAGE:CXX>,$<CONFIG:Debug>>:/MTd>"
      "$<$<AND:$<COMPILE_LANGUAGE:CXX>,$<CONFIG:Release>>:/MT>"
  )
endforeach()

add_custom_command(TARGET crtsys_wfp_bind_redirect POST_BUILD
  COMMAND "${CMAKE_COMMAND}" -E copy_if_different
          "${CMAKE_CURRENT_LIST_DIR}/crtsys_wfp_bind_redirect.inf"
          "$<TARGET_FILE_DIR:crtsys_wfp_bind_redirect>"
)

include("${crtsys_SOURCE_DIR}/cmake/std-cxx.cmake")
set_std_cxx_latest(crtsys_wfp_bind_redirect)
set_std_cxx_latest(crtsys_wfp_bind_redirect_controller)
set_std_cxx_latest(crtsys_wfp_bind_redirect_acceptance)
