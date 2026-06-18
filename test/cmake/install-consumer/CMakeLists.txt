cmake_minimum_required(VERSION 3.14 FATAL_ERROR)

project(crtsys_install_consumer LANGUAGES C CXX)

set(CRTSYS_PACKAGE_ROOT "" CACHE PATH "Installed or packaged crtsys prefix")
if(NOT CRTSYS_PACKAGE_ROOT)
    message(FATAL_ERROR "Set CRTSYS_PACKAGE_ROOT to an installed or unpacked crtsys package prefix.")
endif()

set(CRTSYS_NTL_MAIN ON)
find_package(crtsys CONFIG REQUIRED PATHS "${CRTSYS_PACKAGE_ROOT}" NO_DEFAULT_PATH)

crtsys_add_driver(crtsys_install_consumer src/main.cpp)
