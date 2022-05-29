# crtsys

**C**/C++ Run**t**ime library for **sys**tem file (Windows Kernel Driver)

[![CMake](https://github.com/ntoskrnl7/crtsys/actions/workflows/cmake.yml/badge.svg)](https://github.com/ntoskrnl7/crtsys/actions/workflows/cmake.yml)
![GitHub](https://img.shields.io/github/license/ntoskrnl7/crtsys)
![GitHub release (latest SemVer)](https://img.shields.io/github/v/release/ntoskrnl7/crtsys)
![Windows 7+](https://img.shields.io/badge/Windows-7+-blue?logo=windows&logoColor=white)
![Visual Studio 2017+](https://img.shields.io/badge/Visual%20Studio-2017+-682270?logo=visualstudio&logoColor=682270)
![CMake 3.14+](https://img.shields.io/badge/CMake-3.14+-yellow.svg?logo=cmake&logoColor=white)
![C++ 14+](https://img.shields.io/badge/C++-14+-white.svg?logo=cplusplus&logoColor=blue)

커널 드라이버에서 C++ 및 STL 기능을 사용할 수 있도록 도와주는 CRT 라이브러리 입니다.

- [crtsys](#crtsys)
  - [Overview](#overview)
  - [Requirements](#requirements)
  - [Test Environments](#test-environments)
  - [Goal](#goal)
    - [C++ Standard](#c-standard)
      - [STL](#stl)
    - [C Standard](#c-standard-1)
    - [NTL (NT Template Library)](#ntl-nt-template-library)
  - [Build](#build)
  - [Test](#test)
  - [Usage](#usage)
  - [TODO](#todo)

## Overview

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
      - 라이센스를 변조하는 것과 **(Microsoft CRT 및 STL 소스 코드를 재배포하는 행위는 허가되지 않은것으로 파악됨)**

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

1. Micosoft CRT와 STL을 최대한 비슷하게 지원하기 위해서 Microsoft CRT 소스코드를 사용하긴 하지만 Microsoft Visual Studio가 설치되어있는 디렉토리 내의 소스를 직접 빌드하는 방법으로 처리하여, Visual Studio를 합법적으로 사용하는 사용자는 라이센스 문제 없이 사용 가능합니다.
2. Win32 API를 구현한 [Ldk](https://github.com/ntoskrnl7/Ldk)를 활용하여 많은 범위의 STL 기능을 지원합니다.
3. CMake로 프로젝트를 구성할 수 있으며, [CPM](https://github.com/cpm-cmake/CPM.cmake)을 사용하여 정말 간편하게 라이브러리를 사용할 수 있도록 지원합니다.

## Requirements

- Windows 8 or later
- Visual Studio 2017 or later
- CMake 3.16 or later
- Git

## Test Environments

- Windows 10 x64
- CMake 3.21.4
- Git 2.23.0
- Visual Studio 2017
  - Visual Studio 2017의 CRT 소스 코드는 일부 헤더가 누락되어 빌드가 되지 않아서, UCXXRT의 코드를 일부 사용하여 지원합니다.
  - 추후 누락된 헤더를 직접 작성하여 지원할 예정입니다.
- Visual Studio 2019
- Visual Studio 2022
- VC Tools
  - 14.16.27023
  - 14.24.28314
  - 14.26.28801
  - 14.29.30133
  - 14.31.31103
- Windows Kit
  - 10.0.17763.0
  - 10.0.18362.0
  - 10.0.22000.0

## Goal

이 프로젝트는 커널 드라이벌르 작성할 때, 응용 프로그램에서 C++ 및 STL을 사용하는 것에 근접한 개발 경험을 제공하는것을 목표로합니다.

현재 지원되거나 앞으로 지원할 기능은 아래와 같습니다.

- 체크된 항목은 구현 완료된 항목입니다.
- 테스트 코드가 작성된 항목은 테스트 코드의 링크를 추가하였습니다.

### C++ Standard

[C++ reference](https://en.cppreference.com)를 기준으로 작성하였습니다.

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
  - 직접 구현하기에는 시간이 부족하여 아래 프로젝트의 내용을 참고하엿습니다. 감사합니다! :-)
  - [RetrievAL](https://github.com/SpoilerScriptsGroup/RetrievAL)
  - [musl](https://github.com/bminor/musl)

### NTL (NT Template Library)

커널에서 더 나은 개발 환경을 지원하기 위한 기능을 제공합니다.

- ntl::expand_stack [(tested)](./test/src/ntl.cpp#L5)
  - 스택 크기를 확장하기 위한 함수
  - 기본적으로 커널 스택은 사용자 스레드 스택보다 훨씬 작은 크기를 할당받기 때문에 STL 기능을 사용하거나, 특히 throw를 수행할때 사용하는것이 좋습니다.
- ntl::status
  - NTSTATUS에 대한 클래스
- ntl::driver
  - DRIVER_OBJECT에 대한 클래스
  - 기능
    - [x] DriverUnload [(tested)](./test/src/main.cpp#L25)
    - [ ] DriverDispatch
- ntl::driver_main [(tested)](./test/src/main.cpp#L21)
  - C++ 용 드라이버 진입점
  - ntl::expand_stack 함수로 스택을 최대 크기로 확장하여 호출됩니다.

## Build

이 프로젝트를 직접 빌드하여 lib와 include를 사용하시려면 Microsoft STL 사용을 위해서 포함 경로 설정 및 전처리기 설정 등 복잡한 사전 작업이 필요하므로  **직접 빌드하여 사용하는것보다는 [Usage](#usage)을 참고하여 CPM을 통해서 사용하시는것을 권장합니다.**

그리고 SDK와 WDK의 버전이 다르면 빌드가 실패할 가능성이 높으므로
**가능하다면 SDK와 WDK의 버전이 같은 환경에서 빌드하는것을 권장합니다.**

빌드 방법은 아래와 같습니다.

```Batch
git clone https://github.com/ntoskrnl7/crtsys
cd crtsys
cmake -S . -B build
cmake --build build --config Release
cmake -S . -B build_x86 -A Win32
cmake --build build_x86 --config Release
```

혹은

```Batch
git clone https://github.com/ntoskrnl7/crtsys
cd crtsys
build.bat . x86 release
build.bat . x64 release
```

위 명령을 실행하여 빌드를 하시면 lib/x64/crtsys.lib와 lib/x86/crtsys.lib가 생성됩니다.

lib 디렉토리와 include 디렉토리를 타 프로젝트에서 사용하시면 됩니다.
이 라이브러리를 사용할 프로젝트의 전처리기 정의나 포함 경로 설정 등 프로젝트 설정은 [CMakeLists.txt](./CMakeLists.txt)에서 PUBLIC으로 설정된 항목들을 참고하여 설정하시기 바랍니다.

## Test

1. 아래 명령을 수행하여 라이브러리 및 테스트 코드를 빌드하시기 바랍니다.

    ```Batch
    git clone https://github.com/ntoskrnl7/crtsys
    cd crtsys/test
    cmake -S . -B build_x64
    cmake --build build_x64
    cmake -S . -B build_x86 -A Win32
    cmake --build build_x86
    cmake -S . -B build_ARM -A ARM
    cmake --build build_ARM
    cmake -S . -B build_ARM64 -A ARM64
    cmake --build build_ARM64
    ```

    혹은

    ```Batch
    git clone https://github.com/ntoskrnl7/crtsys
    cd crtsys/test
    build.bat . x86
    build.bat . x64
    build.bat . ARM
    build.bat . ARM64
    ```

    혹은 아래 명령을 수행하시면 지원되는 모든 아키텍쳐에 대해서 Debug, Release 구성을 모두 빌드합니다.

    ```Batch
    git clone https://github.com/ntoskrnl7/crtsys
    cd crtsys/test
    build_all.bat test
    ```

2. build/Debug/crtsys_test.sys를 설치 및 로드하시기 바랍니다.

   x64 : build_x64/Debug/crtsys_test.sys
   x86 : build_x86/Debug/crtsys_test.sys
   ARM : build_ARM/Debug/crtsys_test.sys
   ARM64 : build_ARM64/Debug/crtsys_test.sys

   ```batch
   sc create CrtSysTest binpath= "빌드된 crtsys_test.sys의 전체 경로" displayname= "crtsys test" start= demand type= kernel
   sc start CrtSysTest
   sc stop CrtSysTest
   sc delete CrtSysTest
   ```

3. 정상적으로 로드/언로드가 되었다면 테스트 성공한 것이며, 테스트 내용은 DebugView나 WinDbg를 통해서 확인 가능합니다.

## Usage

CMake를 사용하는것을 권장합니다.

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

    로 CPM.cmake를 다운로드 받으시기 바랍니다.

3. 프로젝트 디렉토리에 아래와 같은 파일을 작성해주시기 바랍니다.

   - 디렉토리 구조

      ```tree
      📦test-project
      ┣ 📂src
      ┃ ┗ 📜main.cpp
      ┗ 📜CMakeLists.txt
      ```

   - CMakeLists.txt

        ```CMake
        cmake_minimum_required(VERSION 3.14 FATAL_ERROR)

        project(crtsys_test LANGUAGES C)

        include(cmake/CPM.cmake)

        set(CRTSYS_NTL_MAIN ON) # use ntl::main
        CPMAddPackage("gh:ntoskrnl7/crtsys@0.1.5")
        include(${crtsys_SOURCE_DIR}/cmake/CrtSys.cmake)

        # add driver
        crtsys_add_driver(crtsys_test main.cpp)
        ```

   - src/main.cpp

        - 아래와 같이 CRTSYS_NTL_MAIN을 활성화한다면 ntl::main을 진입점으로 정의하시기 바랍니다. **(권장)**

          ```CMake
          set(CRTSYS_NTL_MAIN ON)
          ```

        - 만약 아래와 같이 CRTSYS_NTL_MAIN을 비활성화한다면 기존과 깉이 DriverEntry를 진입점으로 정의하시기 바랍니다.

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
   sc create CrtSysTest binpath= "빌드된 crtsys_test.sys의 전체 경로" displayname= "crtsys test" start= demand type= kernel
   sc start CrtSysTest
   sc stop CrtSysTest
   sc delete CrtSysTest
   ```

## TODO

- CMake Install 구현
- ARM/ARM64 장치에서 테스트 수행
- 아직 구현되지 않은 C++ 및 STL 기능 구현
- Visual Studio 2017의 CRT 소스 코드 빌드
- 커널 드라이버와 사용자 프로세스 간 통신 기능
