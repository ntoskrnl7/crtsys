cmake_minimum_required(VERSION 3.14 FATAL_ERROR)

project(crtsys_offline_consumer LANGUAGES C CXX)

set(CRTSYS_PACKAGE_ROOT "" CACHE PATH "Installed or packaged crtsys prefix")

if(NOT CRTSYS_PACKAGE_ROOT)
  message(FATAL_ERROR "CRTSYS_PACKAGE_ROOT must point to an installed crtsys prefix")
endif()

set(CRTSYS_NTL_MAIN ON)
find_package(crtsys CONFIG REQUIRED PATHS "${CRTSYS_PACKAGE_ROOT}" NO_DEFAULT_PATH)

crtsys_add_driver(crtsys_offline_consumer src/main.cpp)
