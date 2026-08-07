cmake_minimum_required(VERSION 3.14 FATAL_ERROR)
project(crtsys_wfp_connect_redirect_acceptance LANGUAGES CXX)
add_executable(crtsys_wfp_connect_redirect_acceptance main.cpp)
target_include_directories(crtsys_wfp_connect_redirect_acceptance PRIVATE
  "${CMAKE_CURRENT_LIST_DIR}/../..")
target_link_libraries(crtsys_wfp_connect_redirect_acceptance PRIVATE ws2_32)
target_compile_options(crtsys_wfp_connect_redirect_acceptance PRIVATE
  /W4 /WX /EHsc
  "$<$<CONFIG:Debug>:/MTd>"
  "$<$<CONFIG:Release>:/MT>")
set_property(TARGET crtsys_wfp_connect_redirect_acceptance
  PROPERTY CXX_STANDARD 23)
set_property(TARGET crtsys_wfp_connect_redirect_acceptance
  PROPERTY CXX_STANDARD_REQUIRED YES)
