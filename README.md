# crtsys

**C**/C++ Run**t**ime library for **sys**tem file (Windows Kernel Driver)

[![CMake](https://github.com/ntoskrnl7/crtsys/actions/workflows/cmake.yml/badge.svg)](https://github.com/ntoskrnl7/crtsys/actions/workflows/cmake.yml)
![GitHub](https://img.shields.io/github/license/ntoskrnl7/crtsys)
![GitHub release (latest SemVer)](https://img.shields.io/github/v/release/ntoskrnl7/crtsys)
![Windows 7+](https://img.shields.io/badge/Windows-7+-blue?logo=windows&logoColor=white)
![Visual Studio 2017+](https://img.shields.io/badge/Visual%20Studio-2017+-682270?logo=visualstudio&logoColor=682270)
![CMake 3.14+](https://img.shields.io/badge/CMake-3.14+-yellow.svg?logo=cmake&logoColor=white)
![C++ 14+](https://img.shields.io/badge/C++-14+-white.svg?logo=cplusplus&logoColor=blue)
![Architecture](https://img.shields.io/badge/CPU-x86%20%2F%20x64%20%2F%20ARM%20%2F%20ARM64-blue.svg?logo=data:image/svg+xml;base64,PD94bWwgdmVyc2lvbj0iMS4wIj8+CjxzdmcgZW5hYmxlLWJhY2tncm91bmQ9Im5ldyAwIDAgNTIgNTIiIGlkPSJMYXllcl8xIiB2ZXJzaW9uPSIxLjEiIHZpZXdCb3g9IjAgMCA1MiA1MiIgeG1sOnNwYWNlPSJwcmVzZXJ2ZSIgeG1sbnM9Imh0dHA6Ly93d3cudzMub3JnLzIwMDAvc3ZnIiB4bWxuczp4bGluaz0iaHR0cDovL3d3dy53My5vcmcvMTk5OS94bGluayI+CiAgICA8Zz4KICAgICAgICA8cGF0aCBmaWxsPSJ3aGl0ZSIgZD0iTTQ5LDE5LjV2LTJoLTYuOTk4NDEzMXYtNy40OTU3ODg2SDE0LjQwMTYxMTNsLTQuNDAwMDI0NCw0LjUzMDAyOTN2MjcuNDY5OTcwN0gxNy41VjQ5aDJ2LTYuOTk1Nzg4NmgzVjQ5aDJ2LTYuOTk1Nzg4NmgzICAgVjQ5aDJ2LTYuOTk1Nzg4NmgzVjQ5aDJ2LTYuOTk1Nzg4NmgyLjgxMTU4NDVsNC42OTAwMDI0LTUuNjQwMDE0NlYzNC41SDQ5di0yaC02Ljk5ODQxMzF2LTNINDl2LTJoLTYuOTk4NDEzMXYtM0g0OXYtMmgtNi45OTg0MTMxICAgdi0zSDQ5eiBNMzYuMDAxNTg2OSwzMy41MDQyMTE0YzAsMS42NTAwMjQ0LTEuMzQ5OTc1NiwzLTMsM2gtMTRjLTEuNjU5OTczMSwwLTMtMS4zNDk5NzU2LTMtM3YtMTRjMC0xLjY1OTk3MzEsMS4zNDAwMjY5LTMsMy0zaDE0ICAgYzEuNjUwMDI0NCwwLDMsMS4zNDAwMjY5LDMsM1YzMy41MDQyMTE0eiIgLz4KICAgICAgICA8cmVjdCBzdHlsZT0iZmlsbDp3aGl0ZSIgaGVpZ2h0PSIyIiB3aWR0aD0iNyIgeD0iMyIgeT0iMTcuNSIgLz4KICAgICAgICA8cmVjdCBzdHlsZT0iZmlsbDp3aGl0ZSIgaGVpZ2h0PSIyIiB3aWR0aD0iNyIgeD0iMyIgeT0iMjIuNSIgLz4KICAgICAgICA8cmVjdCBzdHlsZT0iZmlsbDp3aGl0ZSIgaGVpZ2h0PSIyIiB3aWR0aD0iNyIgeD0iMyIgeT0iMjcuNSIgLz4KICAgICAgICA8cmVjdCBzdHlsZT0iZmlsbDp3aGl0ZSIgaGVpZ2h0PSIyIiB3aWR0aD0iNyIgeD0iMyIgeT0iMzIuNSIgLz4KICAgICAgICA8cmVjdCBzdHlsZT0iZmlsbDp3aGl0ZSIgaGVpZ2h0PSI3IiB3aWR0aD0iMiIgeD0iMzIuNSIgeT0iMyIgLz4KICAgICAgICA8cmVjdCBzdHlsZT0iZmlsbDp3aGl0ZSIgaGVpZ2h0PSI3IiB3aWR0aD0iMiIgeD0iMjcuNSIgeT0iMyIgLz4KICAgICAgICA8cmVjdCBzdHlsZT0iZmlsbDp3aGl0ZSIgaGVpZ2h0PSI3IiB3aWR0aD0iMiIgeD0iMjIuNSIgeT0iMyIgLz4KICAgICAgICA8cmVjdCBzdHlsZT0iZmlsbDp3aGl0ZSIgaGVpZ2h0PSI3IiB3aWR0aD0iMiIgeD0iMTcuNSIgeT0iMyIgLz4KICAgIDwvZz4KPC9zdmc+)

**crtsys** is open source library that helps you use C++ CRT and STL features in your kernel drivers.

- [crtsys](#crtsys)
  - [Overview](#overview)
  - [Goal](#goal)
    - [C++ Standard](#c-standard)
      - [STL](#stl)
    - [C Standard](#c-standard-1)
    - [NTL (NT Template Library)](#ntl-nt-template-library)
  - [Requirements](#requirements)
  - [Test Environments](#test-environments)
  - [Build & Test](#build--test)
  - [Usage](#usage)
  - [TODO](#todo)

## Overview

This project has the most support for C++ STL features among similar projects.

<details>
<summary>more</summary>

커널 드라이버에서 C++ 및 STL을 사용할수있도록 도와주는 기능을 조사해보았는데, 그중 사용하기 가장 잘 구현되어있는 프로젝트는 아래와 같습니다.

(22-04-26 기준)

- [UCXXRT](https://github.com/MiroKaku/ucxxrt)
  - 장점
    - 타 프로젝트와는 다르게 [Microsoft STL](https://github.com/microsoft/STL)를 직접 사용한다는 개념 자체가 좋아보임.
  - 단점
    - 현재까지는 Win32 API와 연관성이 적은 std::string, std::vector 클래스 등 자료형이나 자료구조 관련 기능만 지원됨.
    - vcxproj이 개발 환경에 종속되기 때문에 VS 버전이 바뀌면 일일이 수작업으로 수정해줘야함.
    - x86에서 [이 코드](https://en.cppreference.com/w/cpp/language/throw#Example)를 실행하는 중 Hang이 발생함
    - Microsoft의 소스 코드를 그대로 복사하여 상단에 자신의 라이센스 주석만 넣어서 프로젝트에 포함시킨것으로 보이며 라이센스 문제에서 자유롭지 못해보임
      - 라이센스를 변경하는 것과 **(Microsoft CRT 및 STL 소스 코드를 재배포하는 행위는 허가되지 않은것으로 파악됨)**

- [KTL](https://github.com/DymOK93/KTL)
  - 장점
    - CMake 사용됨
      - 개발 환경에 맞는 프로젝트 파일 생성 및 자동화가 용이함
    - CRT를 포함한 모든 코드를 독자적으로 구현
      - 라이센스로부터 자유로우며, 예외처리 코드 부분이 경량화되어 스택을 절약할 수 있음
    - 커널에 특화된 템플릿 라이브러리 제공
      - 커널에서만 존재하는 매커니즘에 대한 템플릿 라이브러리를 제공하기 때문에, 실제 드라이버 제작 시에는 STL을 보다도 더 애용할만한 기능이 많음
    - 높은 품질의 코드
  - 단점
    - Microsoft STL을 사용하는 것을 고려해서 작성된것으로 보이나 실제로 Microsoft STL을 사용하도록 변경하는 것에 어려움을 느낌 (오탈자 및 ktl 만 사용하는 코드가 생각보다 많았음)
    - 러시아어 주석인데 파일이 UTF8+BOM로 인코딩되어있지 않아서 러시아 외 국가 환경에서 빌드를 실패함
    - x86 미지원

각 라이브러리의 단점을 보완하는 작업을 진행하던 중

1. UCXXRT은 Microsoft CRT 소스 코드를 그대로 복사하여 프로젝트에 포함시켰다는 점
2. KTL은 x86을 지원하지 않는 점

으로 인해서 새로운 라이브러리를 개발하게 되었습니다.

crtsys의 장점은 아래와 같습니다.

1. Micosoft CRT와 STL을 최대한 비슷하게 지원하기 위해서 Microsoft CRT 소스코드를 사용하긴 하지만 Microsoft Visual Studio 혹은 Build Tools가 설치되어있는 디렉토리 내의 소스를 직접 빌드하는 방법으로 처리하여, Visual Studio를 합법적으로 사용하는 사용자는 라이센스 문제 없이 사용 가능합니다.
2. Win32 API를 구현한 [Ldk](https://github.com/ntoskrnl7/Ldk)를 활용하여 많은 범위의 STL 기능을 지원합니다.
3. CMake로 프로젝트를 구성할 수 있으며, [CPM](https://github.com/cpm-cmake/CPM.cmake)을 사용하여 정말 간편하게 라이브러리를 사용할 수 있도록 지원합니다.

</details>

## Goal

This project aims to provide a development experience similar to using C++ and STL in your applications when writing kernel drivers.

Features currently supported or supported in the future are listed below.

- Checked items are items that have been implemented.
- For the item where the test code is written, a link to the test code is added.

### C++ Standard

It was written based on the [C++ reference](https://en.cppreference.com)

- [Initialization](https://en.cppreference.com/w/cpp/language/initialization)
  - [x] [Non-local variables](https://en.cppreference.com/w/cpp/language/initialization#Non-local_variables)
    - [x] [Static initialization](https://en.cppreference.com/w/cpp/language/initialization#Static_initialization)
      - [x] [Constant initialization](https://en.cppreference.com/w/cpp/language/constant_initialization) [(tested)](./test/src/cpp/lang/initialization.cpp#L13)
      - [x] [Zero initialization](https://en.cppreference.com/w/cpp/language/zero_initialization) [(tested)](./test/src/cpp/lang/initialization.cpp#L41)
    - [x] [Dynamic initialization](https://en.cppreference.com/w/cpp/language/initialization#Dynamic_initialization) [(tested)](./test/src/cpp/lang/initialization.cpp#L65)
  - [ ] [Static local variables](https://en.cppreference.com/w/cpp/language/storage_duration#Static_local_variables)
    - [ ] thread_local
    - [ ] static
- [Exceptions](https://en.cppreference.com/w/cpp/language/exceptions)
  - [x] [throw](https://en.cppreference.com/w/cpp/language/throw) [(tested)](./test/src/cpp/lang/exceptions.cpp#L42)
  - [x] [try-block](https://en.cppreference.com/w/cpp/language/try_catch) [(tested)](./test/src/cpp/lang/exceptions.cpp#L60)
  - [x] [Function-try-block](https://en.cppreference.com/w/cpp/language/function-try-block) [(tested)](./test/src/cpp/lang/exceptions.cpp#L98)

#### STL

- [x] [std::chrono](https://en.cppreference.com/w/cpp/chrono) [(tested)](./test/src/cpp/stl/chrono.cpp#L15)
- [x] [std::thread](https://en.cppreference.com/w/cpp/thread) [(tested)](./test/src/cpp/stl/thread.cpp#L35)
- [x] [std::condition_variable](https://en.cppreference.com/w/cpp/thread/condition_variable) [(tested)](./test/src/cpp/stl/thread.cpp#L35)
- [x] [std::mutex](https://en.cppreference.com/w/cpp/thread/mutex) [(tested)](./test/src/cpp/stl/thread.cpp#L81)
- [x] [std::shared_mutex](https://en.cppreference.com/w/cpp/thread/shared_mutex) [(tested)](./test/src/cpp/stl/thread.cpp#L129)
- [x] [std::future](https://en.cppreference.com/w/cpp/thread/future) [(tested)](./test/src/cpp/stl/thread.cpp#L157)
- [x] [std::promise](https://en.cppreference.com/w/cpp/thread/promise) [(tested)](./test/src/cpp/stl/thread.cpp#L203)
- [x] [std::packaged_task](https://en.cppreference.com/w/cpp/thread/packaged_task) [(tested)](./test/src/cpp/stl/thread.cpp#L267)
- [x] [std::cin](https://en.cppreference.com/w/cpp/io/cin)
- [x] [std::clog](https://en.cppreference.com/w/cpp/io/clog)
- [x] [std::cerr](https://en.cppreference.com/w/cpp/io/cerr)
- [x] [std::cout](https://en.cppreference.com/w/cpp/io/cout) (tested)
- [x] [std::wcin](https://en.cppreference.com/w/cpp/io/cin)
- [x] [std::wclog](https://en.cppreference.com/w/cpp/io/clog)
- [x] [std::wcerr](https://en.cppreference.com/w/cpp/io/cerr)
- [x] [std::wcout](https://en.cppreference.com/w/cpp/io/cout) (tested)

### C Standard

- [x] math functions
  - I did not have enough time to implement it myself, so I referred to the contents of the project below. thank you! :-)
  - [RetrievAL](https://github.com/SpoilerScriptsGroup/RetrievAL)
  - [musl](https://github.com/bminor/musl)

### NTL (NT Template Library)

Provides features to support a better development environment in the kernel.

- ntl::expand_stack [(tested)](./test/src/ntl.cpp#L5)
  - Function to extend the stack size
  - By default, the kernel stack is allocated a much smaller size than the user thread stack, so it is recommended to use the STL function or especially when performing throw.
- ntl::status
  - Class for NTSTATUS
- ntl::driver
  - Class for DRIVER_OBJECT
  - Features
    - [x] DriverUnload [(tested)](./test/driver/src/main.cpp#L30)
    - [ ] DriverDispatch
- ntl::driver_main [(tested)](./test/driver/src/main.cpp#L22)
  - Driver entry point for C++.
  - Called by expanding the stack to its maximum size with the ntl::expand_stack function.
- ntl::rpc
  - Provides an easy communication method between User Mode Application and Kernel Driver.
    - ntl::rpc::server [(tested)](./test/driver/src/main.cpp#L19) [(tested)](./test/common/rpc.hpp)
    - ntl::rpc::client [(tested)](./test/app/src/main.cpp#L1) [(tested)](./test/common/rpc.hpp)
      - I did not have enough time to implement the data serialization part myself, so I referred to the contents of the project below. thank you! :-)
        - [Eyal Z/zpp serializer](https://github.com/eyalz800/serializer)

## Requirements

- Windows 7 or later
- [Visual Studio / Build Tools 2017 or later](https://visualstudio.microsoft.com/ko/downloads/)
- [CMake 3.16 or later](https://cmake.org/download/)
- [Git](https://git-scm.com/downloads)

## Test Environments

- Windows 10 x64
  - It can be built with **x86, x64, ARM, ARM64**, but the actual test has only been validated against x86 and x64 modules.

- CMake 3.21.4
- Git 2.23.0
- Visual Studio 2017
  - Visual Studio 2017's CRT source code was missing some headers and could not be built, so it is supported using some of UCXXRT code.
  - In the future, we plan to support by manually writing the missing header.
- Visual Studio 2019
- Visual Studio 2022
- VC Tools
  - 14.16.27023
  - 14.24.28314
  - 14.26.28801
  - 14.29.30133
  - 14.31.31103
- Windows Kit (SDK, WDK)
  - 10.0.17763.0
  - 10.0.18362.0
  - 10.0.22000.0

If the SDK and WDK versions are different, builds are more likely to fail. **If possible, it is recommended to build in the same environment as the SDK and WDK versions.**

## Build & Test

1. Please build the library and test code by executing the command below.

    ```Batch
    git clone https://github.com/ntoskrnl7/crtsys
    cd crtsys\test\build.bat
    ```

    Alternatively, if you execute the command below, both Debug and Release configurations are built for all supported architectures.

    ```Batch
    git clone https://github.com/ntoskrnl7/crtsys
    cd crtsys/test
    build_all.bat test\app
    build_all.bat test\driver
    ```

2. Install and load build\Debug\crtsys_test.sys.

   - driver
    x64 : test\driver\build_x64\Debug\crtsys_test.sys
    x86 : test\driver\build_x86\Debug\crtsys_test.sys
    ARM : test\driver\build_ARM\Debug\crtsys_test.sys
    ARM64 : test\driver\build_ARM64\Debug\crtsys_test.sys
   - app
    x64 : test\driver\build_x64\Debug\crtsys_test_app.exe
    x86 : test\driver\build_x86\Debug\crtsys_test_app.exe
    ARM : test\driver\build_ARM\Debug\crtsys_test_app.exe
    ARM64 : test\driver\build_ARM64\Debug\crtsys_test_app.exe

   ```batch
   sc create CrtSysTest binpath= "crtsys_test.sys full path" displayname= "crtsys test" start= demand type= kernel
   sc start CrtSysTest

   crtsys_test.app.exe

   sc stop CrtSysTest
   sc delete CrtSysTest
   ```

3. If it loads normally and passes/unloads Google Test, the test is successful, and the test contents can be checked through DebugView or WinDbg.

## Usage

1. Please move after creating the project directory.

   ```batch
   mkdir test-project
   cd test-project
   ```

2. Download CPM to your project directory.

    ```batch
    mkdir cmake
    wget -O cmake/CPM.cmake https://github.com/cpm-cmake/CPM.cmake/releases/latest/download/get_cpm.cmake
    ```

    or

    ```batch
    mkdir cmake
    curl -o cmake/CPM.cmake -LJO https://github.com/cpm-cmake/CPM.cmake/releases/latest/download/get_cpm.cmake
    ```

3. Please write the following files in the project directory.

   - Directory structure

      ```tree
      📦test-project
      ┣ 📂src
      ┃ ┗ 📜main.cpp
      ┗ 📜CMakeLists.txt
      ```

   - CMakeLists.txt

        ```CMake
        cmake_minimum_required(VERSION 3.14 FATAL_ERROR)

        project(crtsys_test LANGUAGES C CXX)

        include(cmake/CPM.cmake)

        set(CRTSYS_NTL_MAIN ON) # use ntl::main
        CPMAddPackage("gh:ntoskrnl7/crtsys@0.1.6")
        include(${crtsys_SOURCE_DIR}/cmake/CrtSys.cmake)

        # add driver
        crtsys_add_driver(crtsys_test src/main.cpp)
        ```

   - src/main.cpp

        - If you enable CRTSYS_NTL_MAIN as shown below, define ntl::main as the entry point. **(recommend)**

          ```CMake
          set(CRTSYS_NTL_MAIN ON)
          ```

        - If you disable CRTSYS_NTL_MAIN as shown below, define DriverEntry as the entry point, which is different from the previous one.

          ```CMake
          set(CRTSYS_NTL_MAIN OFF)
          ```

        Below is example code from a project with ntl::main set as entry point.

        ```C
        #include <iostream>
        #include <ntl/driver>

        ntl::status ntl::main(ntl::driver &driver, const std::wstring &registry_path) {

          std::wcout << "load (registry_path :" << registry_path << ")\n";

          // TODO

          driver.on_unload([registry_path]() {
            std::wcout << "unload (registry_path :" << registry_path << ")\n";
          });

          return status::ok();
        }
        ```

4. Perform a build.

   ```batch
   cmake -S . -B build
   cmake --build build
   ```

5. Please check if the driver starts up and shuts down normally.

   ```batch
   sc create CrtSysTest binpath= "crtsys_test.sys full path" displayname= "crtsys test" start= demand type= kernel
   sc start CrtSysTest
   sc stop CrtSysTest
   sc delete CrtSysTest
   ```

## TODO

- CMake install handling.
- Implementing C++ STL features not yet implemented.
- Build CRT source code in Visual Studio 2017.
- Running unit tests in GitHub Action.
