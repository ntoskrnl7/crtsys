cmake_minimum_required(VERSION 3.14 FATAL_ERROR)

project(crtsys_install_consumer LANGUAGES C CXX)

set(CRTSYS_PACKAGE_ROOT "" CACHE PATH "Installed or packaged crtsys prefix")

set(CRTSYS_NTL_MAIN ON)
if(CRTSYS_PACKAGE_ROOT)
  find_package(crtsys CONFIG REQUIRED PATHS "${CRTSYS_PACKAGE_ROOT}" NO_DEFAULT_PATH)
else()
  find_package(crtsys CONFIG REQUIRED)
  set(CRTSYS_PACKAGE_ROOT "${crtsys_ROOT}")
endif()

crtsys_add_driver(crtsys_install_consumer src/main.cpp)
crtsys_add_driver(
  crtsys_install_kernel_msquic_consumer
  src/kernel_msquic.cpp
  WFP
  NTL
  KERNEL_MSQUIC
  KERNEL_CONTENT_CODECS
)

include(
  "${CRTSYS_PACKAGE_ROOT}/share/crtsys/cmake/NtlContentCodecs.cmake"
)
add_executable(
  crtsys_http_transform_consumer
  src/http_transform.cpp
)
target_compile_features(
  crtsys_http_transform_consumer PRIVATE cxx_std_20
)
target_include_directories(
  crtsys_http_transform_consumer
  PRIVATE "${CRTSYS_PACKAGE_ROOT}/include"
)
if(CMAKE_GENERATOR_PLATFORM MATCHES "^ARM64")
  set(crtsys_http_transform_architecture _ARM64_)
elseif(CMAKE_GENERATOR_PLATFORM MATCHES "^ARM")
  set(crtsys_http_transform_architecture _ARM_)
elseif(CMAKE_SIZEOF_VOID_P EQUAL 8)
  set(crtsys_http_transform_architecture _AMD64_)
else()
  set(crtsys_http_transform_architecture _X86_)
endif()
target_compile_definitions(
  crtsys_http_transform_consumer
  PRIVATE ${crtsys_http_transform_architecture}
)
target_link_libraries(
  crtsys_http_transform_consumer
  PRIVATE crtsys_ntl_content_codecs
)
