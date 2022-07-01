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

* [한국어](../docs/ko-kr.md)

**crtsys**는 커널 드라이버에서 C++ CRT 및 STL 기능을 사용할 수 있도록 도와주는 오픈소스 라이브러리입니다.

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

이 프로젝트는 유사한 프로젝트 중 가장 많은 C++ STL 기능을 지원합니다.

<details>
<summary>more</summary>

커널 드라이버에서 C++ 및 STL을 사용할수있도록 도와주는 기능을 조사해보았는데, 그중 사용하기 가장 잘 구현되어있는 프로젝트는 아래와 같습니다.

(22-04-26 기준)

* [UCXXRT](https://github.com/MiroKaku/ucxxrt)
  * 장점
    * 타 프로젝트와는 다르게 [Microsoft STL](https://github.com/microsoft/STL)를 직접 사용한다는 개념 자체가 좋아보임.
  * 단점
    * 현재까지는 Win32 API와 연관성이 적은 std::string, std::vector 클래스 등 자료형이나 자료구조 관련 기능만 지원됨.
    * vcxproj이 개발 환경에 종속되기 때문에 VS 버전이 바뀌면 일일이 수작업으로 수정해줘야함.
    * x86에서 [이 코드](https://en.cppreference.com/w/cpp/language/throw#Example)를 실행하는 중 Hang이 발생함
    * Microsoft의 소스 코드를 그대로 복사하여 상단에 자신의 라이센스 주석만 넣어서 프로젝트에 포함시킨것으로 보이며 라이센스 문제에서 자유롭지 못해보임
      * 라이센스를 변경하는 것과 Microsoft CRT 및 STL 소스 코드를 재배포하는 행위는 허가되지 않은것으로 파악됨

* [KTL](https://github.com/DymOK93/KTL)
  * 장점
    * CMake 사용됨
      * 개발 환경에 맞는 프로젝트 파일 생성 및 자동화가 용이함
    * CRT를 포함한 모든 코드를 독자적으로 구현
      * 라이센스로부터 자유로우며, 예외처리 코드 부분이 경량화되어 스택을 절약할 수 있음
    * 커널에 특화된 템플릿 라이브러리 제공
      * 커널에서만 존재하는 매커니즘에 대한 템플릿 라이브러리를 제공하기 때문에, 실제 드라이버 제작 시에는 STL을 보다도 더 애용할만한 기능이 많음
    * 높은 품질의 코드
  * 단점
    * Microsoft STL을 사용하는 것을 고려해서 작성된것으로 보이나 실제로 Microsoft STL을 사용하도록 변경하는 것에 어려움을 느낌 (오탈자 및 ktl 만 사용하는 코드가 생각보다 많았음)
    * 러시아어 주석인데 파일이 UTF8+BOM로 인코딩되어있지 않아서 러시아 외 국가 환경에서 빌드를 실패함
    * x86 미지원

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

이 프로젝트는 커널 드라이버를 작성할 때 애플리케이션에서 C++ 및 STL을 사용하는 것과 유사한 개발 경험을 제공하는 것을 목표로 합니다.

현재 지원되거나 향후 지원되는 기능은 다음과 같습니다.

* 체크된 항목은 구현된 항목입니다.
* 테스트 코드가 작성된 항목의 경우 테스트 코드에 대한 링크가 추가되었습니다.

### C++ Standard

테스트 코드는 [C++ reference](https://en.cppreference.com)를 기반으로 작성되었습니다.

* [Initialization](https://en.cppreference.com/w/cpp/language/initialization)
  * [x] [Non-local variables](https://en.cppreference.com/w/cpp/language/initialization#Non-local_variables)
    * [x] [Static initialization](https://en.cppreference.com/w/cpp/language/initialization#Static_initialization)
      * [x] [Constant initialization](https://en.cppreference.com/w/cpp/language/constant_initialization) [(tested)](../test/driver/src/cpp/lang/initialization.cpp#L13)
      * [x] [Zero initialization](https://en.cppreference.com/w/cpp/language/zero_initialization) [(tested)](../test/driver/src/cpp/lang/initialization.cpp#L41)
    * [x] [Dynamic initialization](https://en.cppreference.com/w/cpp/language/initialization#Dynamic_initialization) [(tested)](../test/driver/src/cpp/lang/initialization.cpp#L65)
  * [ ] [Static local variables](https://en.cppreference.com/w/cpp/language/storage_duration#Static_local_variables)
    * [ ] thread_local
    * [ ] static
* [Exceptions](https://en.cppreference.com/w/cpp/language/exceptions)
  * [x] [throw](https://en.cppreference.com/w/cpp/language/throw) [(tested)](../test/driver/src/cpp/lang/exceptions.cpp#L42)
  * [x] [try-block](https://en.cppreference.com/w/cpp/language/try_catch) [(tested)](../test/driver/src/cpp/lang/exceptions.cpp#L60)
  * [x] [Function-try-block](https://en.cppreference.com/w/cpp/language/function-try-block) [(tested)](../test/driver/src/cpp/lang/exceptions.cpp#L98)

#### STL

* [x] [std::chrono](https://en.cppreference.com/w/cpp/chrono) [(tested)](../test/driver/src/cpp/stl/chrono.cpp#L15)
* [x] [std::thread](https://en.cppreference.com/w/cpp/thread) [(tested)](../test/driver/src/cpp/stl/thread.cpp#L35)
* [x] [std::condition_variable](https://en.cppreference.com/w/cpp/thread/condition_variable) [(tested)](../test/driver/src/cpp/stl/thread.cpp#L35)
* [x] [std::mutex](https://en.cppreference.com/w/cpp/thread/mutex) [(tested)](../test/driver/src/cpp/stl/thread.cpp#L81)
* [x] [std::shared_mutex](https://en.cppreference.com/w/cpp/thread/shared_mutex) [(tested)](../test/driver/src/cpp/stl/thread.cpp#L129)
* [x] [std::future](https://en.cppreference.com/w/cpp/thread/future) [(tested)](../test/driver/src/cpp/stl/thread.cpp#L157)
* [x] [std::promise](https://en.cppreference.com/w/cpp/thread/promise) [(tested)](../test/driver/src/cpp/stl/thread.cpp#L203)
* [x] [std::packaged_task](https://en.cppreference.com/w/cpp/thread/packaged_task) [(tested)](../test/driver/src/cpp/stl/thread.cpp#L267)
* [x] [std::cin](https://en.cppreference.com/w/cpp/io/cin)
* [x] [std::clog](https://en.cppreference.com/w/cpp/io/clog)
* [x] [std::cerr](https://en.cppreference.com/w/cpp/io/cerr)
* [x] [std::cout](https://en.cppreference.com/w/cpp/io/cout) (tested)
* [x] [std::wcin](https://en.cppreference.com/w/cpp/io/cin)
* [x] [std::wclog](https://en.cppreference.com/w/cpp/io/clog)
* [x] [std::wcerr](https://en.cppreference.com/w/cpp/io/cerr)
* [x] [std::wcout](https://en.cppreference.com/w/cpp/io/cout) (tested)

### C Standard

* [x] math functions
  * 직접 구현하기에는 시간이 부족하여 아래 프로젝트의 내용을 참고하엿습니다. 감사합니다! :-)
  * [RetrievAL](https://github.com/SpoilerScriptsGroup/RetrievAL)
  * [musl](https://github.com/bminor/musl)

### NTL (NT Template Library)

커널에서 더 나은 개발 환경을 지원하기 위한 기능을 제공합니다.

* ntl::expand_stack [(tested)](../test/src/ntl.cpp#L5)
  * 스택 크기를 확장하기 위한 함수
  * 기본적으로 커널 스택은 사용자 스레드 스택보다 훨씬 작은 크기를 할당받기 때문에 STL 기능을 사용하거나, 특히 throw를 수행할때 사용하는것이 좋습니다.
* ntl::status
  * NTSTATUS에 대한 클래스
* ntl::driver
  * DRIVER_OBJECT에 대한 클래스
  * 기능
    * [x] DriverUnload [(tested)](../test/driver/src/main.cpp#L30)
    * [x] Create device [(tested)](../test/driver/src/main.cpp#L39)
* ntl::device
  * DEVICE_OBJECT에 대한 클래스
  * Features
    * [x] Device Extension [(tested)](../test/driver/src/main.cpp#L39)
    * [ ] IRP_MJ_CREATE
    * [ ] IRP_MJ_CLOSE
    * [x] IRP_MJ_DEVICE_CONTROL [(tested)](../test/app/src/main.cpp#L77) [(tested)](../test/driver/src/main.cpp#L47)
* ntl::driver_main [(tested)](../test/driver/src/main.cpp#L22)
  * C++ 용 드라이버 진입점
  * ntl::expand_stack 함수로 스택을 최대 크기로 확장하여 호출됩니다.
* ntl::rpc
  * User Mode App과 Kernel Driver간 손쉬운 통신 방법을 제공합니다.
    * ntl::rpc::server [(tested)](../test/driver/src/main.cpp#L19) [(tested)](../test/common/rpc.hpp)
    * ntl::rpc::client [(tested)](../test/app/src/main.cpp#L4) [(tested)](../test/common/rpc.hpp)
      * 데이터 직렬화 부분을 직접 구현하기에는 시간이 부족하여 아래 프로젝트 내용을 참고했습니다. 감사합니다! :-)
        * [Eyal Z/zpp serializer](https://github.com/eyalz800/serializer)

## Requirements

* Windows 7 이상
* [Visual Studio / Build Tools 2017 이상](https://visualstudio.microsoft.com/ko/downloads/)
* [CMake 3.16 이상](https://cmake.org/download/)
* [Git](https://git-scm.com/downloads)

## Test Environments

* Windows 10 x64
  * **x86, x64, ARM, ARM64**로 빌드 가능하지만, 실제 테스트는 x86, x64 모듈에 대해서만 검증되었습니다.

* CMake 3.21.4
* Git 2.23.0
* Visual Studio 2017
  * Visual Studio 2017의 CRT 소스 코드는 일부 헤더가 누락되어 빌드가 되지 않아서, UCXXRT의 코드를 일부 사용하여 지원합니다.
  * 추후 누락된 헤더를 직접 작성하여 지원할 예정입니다.
* Visual Studio 2019
* Visual Studio 2022
* VC Tools
  * 14.16.27023
  * 14.24.28314
  * 14.26.28801
  * 14.29.30133
  * 14.31.31103
* Windows Kit (SDK, WDK)
  * 10.0.17763.0
  * 10.0.18362.0
  * 10.0.22000.0

SDK와 WDK의 버전이 다르면 빌드가 실패할 가능성이 높으므로 **가능하다면 SDK와 WDK의 버전이 같은 환경에서 빌드하는것을 권장합니다.**

## Build & Test

1. 아래 명령을 수행하여 라이브러리 및 테스트 코드를 빌드하시기 바랍니다.

    ```Batch
    git clone https://github.com/ntoskrnl7/crtsys
    cd crtsys\test\build.bat
    ```

    혹은 아래 명령을 수행하시면 지원되는 모든 아키텍쳐에 대해서 Debug, Release 구성을 모두 빌드합니다.

    ```Batch
    git clone https://github.com/ntoskrnl7/crtsys
    cd crtsys/test
    build_all.bat test\app
    build_all.bat test\driver
    ```

2. build\Debug\crtsys_test.sys를 설치 및 로드하시기 바랍니다.

   * driver
    x64 : test\driver\build_x64\Debug\crtsys_test.sys
    x86 : test\driver\build_x86\Debug\crtsys_test.sys
    ARM : test\driver\build_ARM\Debug\crtsys_test.sys
    ARM64 : test\driver\build_ARM64\Debug\crtsys_test.sys
   * app
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

3. 정상적으로 로드, Google Test 통과/언로드가 되었다면 테스트 성공한 것이며, 테스트 내용은 DebugView나 WinDbg를 통해서 확인 가능합니다.

## Usage

1. 프로젝트 디렉토리를 생성 후 이동하시기 바랍니다.

   ```batch
   mkdir test-project
   cd test-project
   ```

2. CPM을 프로젝트 디렉토리에 다운로드 받으시기 바랍니다.

    ```batch
    mkdir cmake
    wget -O cmake/CPM.cmake https://github.com/cpm-cmake/CPM.cmake/releases/latest/download/get_cpm.cmake
    ```

    혹은

    ```batch
    mkdir cmake
    curl -o cmake/CPM.cmake -LJO https://github.com/cpm-cmake/CPM.cmake/releases/latest/download/get_cpm.cmake
    ```

3. 프로젝트 디렉토리에 아래와 같은 파일을 작성해주시기 바랍니다.

   * 디렉토리 구조

      ```tree
      📦test-project
      ┣ 📂src
      ┃ ┗ 📜main.cpp
      ┗ 📜CMakeLists.txt
      ```

   * CMakeLists.txt

        ```CMake
        cmake_minimum_required(VERSION 3.14 FATAL_ERROR)

        project(crtsys_test LANGUAGES C CXX)

        include(cmake/CPM.cmake)

        set(CRTSYS_NTL_MAIN ON) # use ntl::main
        CPMAddPackage("gh:ntoskrnl7/crtsys@0.1.7")
        include(${crtsys_SOURCE_DIR}/cmake/CrtSys.cmake)

        # add driver
        crtsys_add_driver(crtsys_test src/main.cpp)
        ```

   * src/main.cpp

        * 아래와 같이 CRTSYS_NTL_MAIN을 활성화한다면 ntl::main을 진입점으로 정의하시기 바랍니다. **(권장)**

          ```CMake
          set(CRTSYS_NTL_MAIN ON)
          ```

        * 만약 아래와 같이 CRTSYS_NTL_MAIN을 비활성화한다면 기존과 깉이 DriverEntry를 진입점으로 정의하시기 바랍니다.

          ```CMake
          set(CRTSYS_NTL_MAIN OFF)
          ```

        아래는 ntl::main를 진입점으로 설정한 프로젝트의 예제 코드입니다.

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

4. 빌드를 수행합니다.

   ```batch
   cmake -S . -B build
   cmake --build build
   ```

5. 드라이버가 정상적으로 시작되고 종료되는지 확인하시기 바랍니다.

   ```batch
   sc create CrtSysTest binpath= "crtsys_test.sys full path" displayname= "crtsys test" start= demand type= kernel
   sc start CrtSysTest
   sc stop CrtSysTest
   sc delete CrtSysTest
   ```

## TODO

* CMake install 처리.
* 아직 구현되지 않은 C++ 및 STL 기능 구현
* Visual Studio 2017의 CRT 소스 코드 빌드
* GitHub Action에서 단위 테스트 실행
