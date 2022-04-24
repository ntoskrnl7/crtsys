# CRTSYS

 커널 드라이버에서 CRT 및 STL을 사용할 수 있습니다.

- [CRTSYS](#crtsys)
  - [Overview](#overview)
  - [Goal](#goal)
    - [C++ Standard](#c-standard)
      - [STL](#stl)
    - [NTL (NT Template Library)](#ntl-nt-template-library)
  - [Requirements](#requirements)
  - [Test Environments](#test-environments)
  - [Build](#build)
  - [Test](#test)
  - [Usage](#usage)
    - [CMake](#cmake)
      - [CMakeLists.txt](#cmakeliststxt)
    - [main.cpp](#maincpp)

## Overview

이미 커널 드라이버에서 C++ 및 STL을 일부 사용할 수 있게 해주는 라이브러리는 존재하지만 아래와 같은 이슈가 있습니다.

- UCXXRT
  - 장점
    - MS STL 지원
    - 추후 다양한 STL 기능을 지원할것으로 보임
  - 단점
    - Win32 API와 연관성이 적은 std::string, std::vector 클래스 등 극히 일부만 지원됨.
    - vcxproj이 개발 환경에 종속되기 때문에 VS 버전이 바뀌면 일일이 수정해줘야함.
    - Microsoft의 소스 코드를 거의 그대로 복사한것으로  라이센스 문제에서 자유롭지 못해보임 **(Microsoft CRT 및 STL 소스 코드를 재배포하는 행위는 불법임)**

- KTL은 CRT를 독자 구현된것으로 보이지만, STL을 사용할 수 없습니다. 코드 품질은 매우 좋지만 러시아어 환경에서만 빌드가 되는 문제가 있습니다.
  - 장점
    - CMake 사용
    - CRT 독자 구현
    - 커널에 특화된 템플릿 라이브러리 제공
    - 높은 품질의 코드
  - 단점
    - MS STL 미지원
    - 러시아어 주석인데 파일이 UTF8+BOM로 인코딩되어있지 않아서 러시아 외 국가 환경에서 빌드를 실패함
    - X86 미지원

이러한 이슈들을 해결하기 위해서 Microsoft Visual Studio가 설치되어있는 디렉토리 내의 소스를 직접 빌드하는 방법으로 처리하여, Visual Studio를 합법적으로 사용하는 사용자는 라이센스 문제 없이 사용 가능합니다.
그리고 Win32 API를 구현한 [Ldk](https://github.com/ntoskrnl7/Ldk)를 활용하여 많은 범위의 MS STL 기능을 지원합니다.

## Goal

응용 프로그램에서 개발하는 수준의 개발 경험을 제공할 것입니다.

### C++ Standard

- [ ] thread_local keyword
- [ ] static keyword
  - [x] Non-local variables
  - [ ] Static local variables
- [x] try-block

#### STL

- [x] [std::chrono](./test/src/std_test.cpp#L126)
- [x] [std::thread](./test/src/std_test.cpp#L92)
- [x] [std::condition_variable](./test/src/std_test.cpp#L92)
- [x] [std::mutex](./test/src/std_test.cpp#L135)
- [x] [std::shared_mutex](./test/src/std_test.cpp#L182)
- [x] [std::future](./test/src/std_test.cpp#L208)
- [x] [std::promise](./test/src/std_test.cpp#L208)
- [x] [std::packaged_task](./test/src/std_test.cpp#L208)
- [ ] std::cout
- [ ] std::cerr

### NTL (NT Template Library)

커널에서 더 나은 개발 환경을 지원하기 위한 기능을 제공합니다.

- ntl::expand_stack
  - 스택 크기를 확장하기 위한 함수
- ntl::status
  - NTSTATUS에 대한 클래스
- ntl::driver
  - DRIVER_OBJECT에 대한 클래스
- ntl::main
  - C++ 용 드라이버 진입점 (스택 크기가 최대 크기로 확장되어 호출됨)

## Requirements

- Windows 8 or later
- [ ] Visual Studio 2017 (현재 UCXXRT의 코드를 일부 사용하여 지원함)
- [x] Visual Studio 2019
- [x] Visual Studio 2022

## Test Environments

- Windows 10
- Visual Studio 2017
- Visual Studio 2019
- Visual Studio 2022

## Build

```Batch
git clone https://github.com/ntoskrnl7/crtsys
cd crtsys
cmake -S . -B build
cmake --build build --config Release
```

## Test

1. 아래 명령을 수행하여 라이브러리 및 테스트 코드를 빌드하시기 바랍니다.

    ```Batch
    git clone https://github.com/ntoskrnl7/crtsys
    cd crtsys/test
    cmake -S . -B build
    cmake --build build
    ```

2. build/Debug/crtsys_test.sys를 설치 및 로드하시기 바랍니다.
3. 정상적으로 로드 및 언로드가 되는지 확인하시기 바랍니다.

## Usage

CMake를 사용하는것을 권장합니다.

### CMake

CMake를 사용하신다면 아래와 같이 CMakeLists.txt를 만드시기 바랍니다.

#### CMakeLists.txt

```CMake
cmake_minimum_required(VERSION 3.14 FATAL_ERROR)

project(crtsys_test LANGUAGES C)

include(cmake/CPM.cmake)

set(CRTSYS_NTL_MAIN ON)
CPMAddPackage("gh:ntoskrnl7/crtsys@0.1.0")
include(${crtsys_SOURCE_DIR}/cmake/CrtSys.cmake)

# add driver
crtsys_add_driver(crtsys_test main.cpp)
```

### main.cpp

간단한 샘플 코드입니다.

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
#include <wdm.h>
#include <ntl/driver>

namespace ntl {
status NTL_API main(ntl::driver &driver, const std::wstring &registry_path) {
  
  // TODO

  driver.on_unload([registry_path]() {
    printf("unload (registry_path : %ws)\n", registry_path.c_str());
  });

  return status::ok();
}
} // namespace ntl
```
