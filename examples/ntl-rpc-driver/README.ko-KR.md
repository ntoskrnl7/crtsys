# NTL RPC 예제 드라이버

[English](./README.md)

이 예제는 NTL RPC 도우미 계층을 보여줍니다. `examples/ntl-driver`의 형식화된
IOCTL 예제와는 의도적으로 분리되어 있습니다. IOCTL 예제는 수동으로 작성하는
장치 제어 계약을 보여주고, 이 예제는 하나의 공유 callback 선언에서 양쪽 구현을
생성합니다.

이 예제는 다음 기능을 보여줍니다.

- C++ 드라이버 진입점인 `ntl::main`
- 드라이버 unload callback이 수명을 소유하는 `ntl::rpc::server`
- 사용자 모드 동반 앱에서 사용하는 `ntl::rpc::client`
- 공유 schema 기반 RPC callback ID와 추론되는 반환 형식
- 직접 생성되는 wrapper와 재사용 가능한 형식화된 client
- 호출 스레드를 다른 작업에 사용할 수 있게 하는 비동기 `OVERLAPPED` RPC
- timeout, `CancelIoEx` 및 협력적 커널 callback 취소
- 서버 callback 실행 전에 검사하는 크기 제한 가변 응답
- 기본적으로 Local System과 Administrators만 접근할 수 있는 보안 control device
- method별 요청 및 decode 할당 한도
- rundown으로 종료를 보호하는 불변 dispatch table
- version, capability 및 method 호환성을 확인하는 시작 시점 계약 검색
- 원래 호출자 신원과 역직렬화 전 method 권한 검사
- 재연결 가능한 client session과 ACK 전까지 재생되는 reliable 알림
- 제한된 backpressure를 적용하는 session 종속 형식화 stream
- 단순 scalar 값, 사용자 정의 요청/응답 쌍 및 `std::vector` 직렬화

소스는 역할별로 나뉩니다.

- `shared/ntl_rpc_sample_types.hpp`: 직렬화되는 요청 및 응답 형식
- `shared/ntl_rpc_sample.hpp`: 드라이버/앱 RPC 계약
- `shared/ntl_rpc_caller_security.hpp`: 호출자 보안 method descriptor
- `driver/caller_security.cpp`: 호출자 검사 및 method 권한 부여
- `driver/operations.cpp`: 커널 callback 구현
- `driver/notifications.cpp`: session 상태 및 reliable 알림 게시
- `shared/ntl_rpc_sample.hpp`: 생성되는 RPC method와 형식화된 stream callback
- `app/caller_security.cpp`: 호출자 보안 client 호출
- `app/synchronous_calls.cpp`: 생성된 wrapper와 재사용 가능한 동기 client
- `app/asynchronous_call.cpp`: 성공하는 비동기 완료
- `app/cancellation.cpp`: timeout 이후 협력적 취소
- `app/coroutine_call.cpp`: C++20 `co_await` 완료
- `app/stop_token_cancellation.cpp`: C++20 `stop_token` 취소
- `app/reliable_notifications.cpp`: subscribe, reconnect, replay 및 ACK
- `app/streaming.cpp`: 형식화된 stream 열기, 쓰기, 읽기, ACK 및 닫기
- `app/streaming_batch.cpp`: 제한된 다중 chunk upload/download batch
- `app/coroutine_task.hpp`: 앱에서 사용하는 최소한의 최상위 coroutine owner
- `app/main.cpp`: 인자 해석, 계약 검증 및 예제 실행 순서

## Visual Studio / NuGet

WDK 워크로드가 설치된 Visual Studio에서
[`crtsys_ntl_rpc_sample_vs.sln`](./crtsys_ntl_rpc_sample_vs.sln)을 여세요.
솔루션에는 다음 프로젝트가 있습니다.

- `crtsys_ntl_rpc_sample`: 커널 RPC 서버 드라이버
- `crtsys_ntl_rpc_sample_app`: 사용자 모드 RPC client 앱

NuGet 패키지를 복원한 다음 `Debug|x64` 또는 `Release|x64`로 빌드하세요. 프로젝트
파일에서는 다음 참조를 사용합니다.

```xml
<PackageReference Include="crtsys" Version="$(CrtSysPackageVersion)" />
```

`CrtSysPackageVersion`의 기본값은 `*`이므로 NuGet 복원 시 구성된 패키지
소스에서 최신 안정 버전의 `crtsys` 패키지를 선택합니다. 재현 가능한 빌드가
필요하면 MSBuild에서 정확한 패키지 버전을 지정하세요.

```bat
msbuild crtsys_ntl_rpc_sample_vs.sln /restore /p:Configuration=Debug /p:Platform=x64 /p:CrtSysPackageVersion=0.1.32
```

## CMake 빌드

저장소 루트에서 다음 명령을 실행합니다.

```bat
cmake -S examples\ntl-rpc-driver -B examples\ntl-rpc-driver\build_x64 -A x64
cmake --build examples\ntl-rpc-driver\build_x64 --config Debug
```

Debug 빌드 결과는 다음과 같습니다.

```text
examples\ntl-rpc-driver\build_x64\Debug\crtsys_ntl_rpc_sample.sys
examples\ntl-rpc-driver\build_x64\Debug\crtsys_ntl_rpc_sample_app.exe
```

기능을 시험하는 동안 진단 중단점을 비활성화하려면 다음과 같이 구성하세요.

```bat
cmake -S examples\ntl-rpc-driver -B examples\ntl-rpc-driver\build_x64 -A x64 -DCRTSYS_ENABLE_DIAGNOSTIC_BREAKPOINTS=OFF
```

## 공유 schema

공유 계약은 [`shared/ntl_rpc_sample.hpp`](./shared/ntl_rpc_sample.hpp)에 있습니다.
macro body는 `<ntl/rpc/server>` 뒤에 포함하면 커널 callback이 되고,
`<ntl/rpc/client>` 뒤에 포함하면 형식화된 사용자 모드 wrapper가 됩니다.

schema는 다음 항목을 노출합니다.

- `crtsys_ntl_rpc_sample::add`
- `crtsys_ntl_rpc_sample::describe`
- `crtsys_ntl_rpc_sample::series`
- `crtsys_ntl_rpc_sample::delayed_add`

schema는 `NTL_RPC_BEGIN_CONTRACT`를 사용해 애플리케이션 계약 version `2`, 예제
capability bit 및 안정적인 명시적 method ID를 게시합니다. 앱은 생성된 첫 wrapper를
호출하기 전에 `client.require_contract()`를 한 번 호출합니다. 따라서 드라이버가
일치하지 않으면 무관한 method 오류 대신 계약 진단과 함께 실패합니다.

공유 헤더는 각 method를 `driver/operations.cpp`의 구현에 연결합니다. 드라이버 쪽
`describe` 작업은 의도적으로 커널 전용 WDK API `KeGetCurrentIrql()`을 호출하고
그 값을 `server_irql`로 반환합니다. 이로써 실행 경계가 분명해집니다. 앱은
직렬화된 응답만 받습니다. 작업은 커널 debugger에서 실행 여부를 확인할 수 있도록
한 줄짜리 `DbgPrint` 메시지도 출력합니다.

`series` callback은 `NTL_RPC_BOUNDED_RESPONSE`로 직렬화된 응답의 최대 크기를
64 KiB로 선언합니다. client는 이 값을 수신 용량으로 사용하고, 서버는 callback을
실행하기 전에 용량을 검증합니다. method에 보안 기본값보다 작은 요청 한도와 decode
할당 한도도 필요하면 `NTL_RPC_METHOD_LIMITS`를 사용하세요. 기본값은
[`docs/ntl/rpc.ko-KR.md`](../../docs/ntl/rpc.ko-KR.md)에 설명되어 있습니다.

macro로 생성된 서버는 공유 schema 등록 후 dispatch table을 고정합니다. 반환된
서버 owner를 드라이버 unload까지 유지하세요. 종료 중에는 NTL이 새 RPC 호출을
거부하고 진행 중인 callback이 끝날 때까지 기다린 뒤 owner가 장치를 해제합니다.

이 예제는 notification channel과 session callback을 `start()` 전에 등록할 수
있도록 `init()` 대신 `crtsys_ntl_rpc_sample::make_server()`를 사용합니다. 계약에
macro로 선언한 method만 필요하다면 일반 `init()` 편의 API를 사용해도 됩니다.

## Reliable 알림 개요

[`app/reliable_notifications.cpp`](./app/reliable_notifications.cpp)는 session을
시작하고 `progress`를 subscribe한 뒤 형식화된 delivery를 수신하고, 의도적으로
ACK 전에 연결을 끊습니다. 두 번째 client가 불투명 token으로 session을 재개하여
같은 sequence를 받고 ACK한 다음 session을 명시적으로 닫습니다.

```cpp
const auto session = client.start_session();
client.subscribe(crtsys_ntl_rpc_sample::progress);

const auto delivery =
    client.receive_reliable(crtsys_ntl_rpc_sample::progress);
client.acknowledge(crtsys_ntl_rpc_sample::progress, delivery);
```

공개 예제를 읽기 쉽게 유지하기 위해 큐 한도, 취소, x86 앱/x64 드라이버 및
lifecycle 전체 사례는
[`test/rpc/notifications`](../../test/rpc/notifications)에 두었습니다.

## 형식화된 streaming

[`app/streaming.cpp`](./app/streaming.cpp)는 stress 로직을 섞지 않고 정상 경로
전체를 보여줍니다. 공유 계약 macro는 하나의 선언에서 드라이버 upload callback
등록과 앱 쪽 open 도우미를 생성합니다.

```cpp
NTL_ADD_STREAM_ID(
    crtsys_ntl_rpc_sample, 0x905, messages,
    ntl_rpc_sample_stream_upload, upload,
    ntl_rpc_sample_stream_download, stream, {
      ntl_rpc_sample_stream_download reply;
      reply.sequence = upload.sequence;
      reply.text = "driver received: " + upload.text;
      stream.write(reply);
      if (upload.finish)
        stream.complete();
    })
```

`upload.finish`는 이 예제의 chunk 형식에 속합니다. 앱은 마지막 upload chunk에서만
이 값을 설정하며, NTL이 추론하거나 삽입하지 않습니다. 드라이버는 이 애플리케이션
수준 종료 표식을 사용해 자체 terminal 출력 record를 큐에 넣습니다.

실행 중 두 방향은 서로 독립적입니다.

```text
app read_async() waits
app write(upload) -------------------------> driver callback
                       stream.write(reply) -> app download queue
app read completes, then app ACKs the reply
                       stream.complete() --> app terminal record
app ACKs the terminal record and closes the stream
```

앱은 각 upload를 보내는 동안 overlapped download read 하나를 대기 상태로
유지합니다.

```cpp
(void)client.start_session();
auto messages = crtsys_ntl_rpc_sample::messages(client);
auto pending_download = messages.read_async();

ntl_rpc_sample_stream_upload upload;
upload.sequence = 1;
upload.text = "chunk 1";
upload.finish = true;
messages.write(upload);

auto reply = pending_download.get();
// Process reply.payload().value() here.
messages.acknowledge(reply);

auto terminal = messages.read();
if (!terminal.payload().is_completed())
  throw std::runtime_error("stream did not complete");
messages.acknowledge(terminal);

messages.close();
client.close_session();
```

download는 ACK 전까지 큐에 남으며 서버의 reliable 알림 한도를 적용받습니다.
upload 호출에는 method 요청/decode 한도와 endpoint pending-call 한도가 적용됩니다.
timeout, 취소, 재연결 replay, terminal record 및 cross-bitness 사례는
[`test/rpc/streaming`](../../test/rpc/streaming)에 있습니다.

드라이버가 `complete()` 또는 `fail()`을 큐에 넣으면 NTL은 해당 stream instance의
이후 upload와 download 및 중복 terminal record를 거부합니다. 앱은 다른 instance를
열기 전에 terminal을 ACK하고 stream을 닫습니다.

[`app/streaming_batch.cpp`](./app/streaming_batch.cpp)는 하나의 `write_batch()`
호출로 upload chunk 세 개를 보내고 `read_batch(4)`로 준비된 응답을 읽습니다.
batch read는 record가 하나라도 준비되면 완료되며 네 slot이 모두 찰 때까지 기다리지
않습니다. 각 record는 자체 replay/ACK sequence를 유지하며
`messages.acknowledge(batch)`가 순서대로 ACK합니다.

예제는 `server_options::asynchronous()`로 endpoint를 초기화합니다. 따라서
애플리케이션 요청은 pending 상태가 된 뒤 PASSIVE_LEVEL work item에서 실행됩니다.
`delayed_add`는 `NTL_ADD_AUTHORIZED_CALLBACK_CONTEXT_ID_3`을 사용합니다. 이 macro는
요청을 decode하기 전에 원래 호출자를 검사하고 커널 구현에 이름이 있는
`ntl::rpc::call_context`를 제공합니다. 작업은 짧은 대기 사이마다 context를
확인하고 앱이 요청을 취소하면 `throw_if_cancelled()`로 빠져나옵니다. 생성되는 앱
API는 계속 `crtsys_ntl_rpc_sample::delayed_add(...)` 및
`delayed_add_async(...)`입니다.

## 호출자 권한 검사 개요

[`driver/caller_security.cpp`](./driver/caller_security.cpp)는 전체 권한 검사 경로를
한곳에 모아 둡니다. 예제 서버가 system worker thread에서 호출을 실행하더라도
정책에는 원래 요청자가 전달됩니다.

```cpp
server->on_authorized(
    crtsys_ntl_rpc_security::user_mode_echo,
    [](const ntl::rpc::call_context &caller) -> NTSTATUS {
      return caller.is_user_mode() ? STATUS_SUCCESS : STATUS_ACCESS_DENIED;
    },
    [](std::uint32_t value) { return value; });
```

앱은 보안 전용 transport 코드 없이 같은 공유 method descriptor를 호출합니다.

```cpp
ntl::rpc::client client(L"crtsys_ntl_rpc_security_sample");
auto caller = client.invoke(crtsys_ntl_rpc_security::caller_info);
auto value = client.invoke(crtsys_ntl_rpc_security::user_mode_echo, 42u);
```

endpoint ACL은 RPC 장치를 열 수 있는 사용자를 결정합니다. `on_authorized()`는
요청 역직렬화와 callback 실행 전에 method별 결정을 추가합니다. 실제 드라이버는
위의 짧은 사용자 모드 정책을 `call_context::check_access()`와 자체 보안 descriptor로
바꿀 수 있습니다. 공개 예제를 읽기 쉽게 유지하기 위해 허용/거부, impersonation 및
callback 억제 전체 사례는 [`test/rpc/security`](../../test/rpc/security)에 있습니다.

## 비동기 모델

NTL RPC는 `std::promise`나 `std::future`로 비동기 호출을 구현하지 않습니다.
`ntl::rpc::async_call<T>`는 요청 하나에 필요한 다음 Windows native 비동기 I/O
상태를 소유합니다.

- `OVERLAPPED` `DeviceIoControl` 요청
- 요청 및 응답 buffer
- 대기 가능한 event
- `GetOverlappedResult`와 `CancelIoEx`에 필요한 device handle

이 형식은 의도적으로 future와 비슷한 `wait()`, `wait_for()`, `get()`을 제공하면서
기반 I/O 요청을 취소하는 `cancel()`도 제공합니다. 표준 `std::future`에는 작업별
Windows I/O 취소 계약이 없으며 buffer와 `OVERLAPPED` 수명 문제도 자체적으로
해결하지 못합니다.

생성된 계약은 서로 대응하는 동기 및 비동기 편의 함수를 제공합니다. `add(40, 2)`는
결과를 직접 반환하고 `delayed_add_async(...)`는 소유권을 가진 `async_call<int>`를
반환합니다.

```cpp
auto call = crtsys_ntl_rpc_sample::delayed_add_async(
    std::uint32_t{100}, 40, 2);

// 이 사이에 호출자가 다른 작업을 수행할 수 있습니다.
if (call.wait_for(std::chrono::seconds(2)) ==
    ntl::rpc::async_wait_status::completed) {
  const int result = call.get();
}
```

두 편의 함수는 독립 호출마다 이름 있는 endpoint를 엽니다. 여러 호출을 연속으로
수행하려면 `ntl::rpc::client` 하나를 유지하고 생성된 `delayed_add_3_method`
descriptor를 `client.invoke_async(...)`와 함께 사용해 같은 연결을 재사용하세요.

취소 예제는 2초짜리 요청을 시작하고 50 ms timeout을 확인한 뒤 `cancel()`을
호출하고, `get()`이 `ERROR_OPERATION_ABORTED`를 보고하는지 검증합니다. 서버
callback은 이름이 있는 context를 확인하므로 단순히 늦은 결과를 버리는 데 그치지
않고 커널 작업을 신속히 중단합니다.

## C++20 stop token과 coroutine

C++14 호환 overload가 기준 API로 유지됩니다. 앱을 C++20 이상으로 컴파일하면 같은
client가 `std::stop_token`도 받습니다.

```cpp
std::stop_source source;
auto call = crtsys_ntl_rpc_sample::delayed_add_async(
    source.get_token(), std::uint32_t{2000}, 40, 2);

source.request_stop(); // 이 호출에 CancelIoEx를 요청합니다.
```

`<ntl/rpc/coroutine>`을 포함하면 native I/O 소유권을 바꾸지 않고 이동 전용 호출을
await할 수 있습니다.

```cpp
auto add_with_coroutine(std::stop_token token) -> application_task<int> {
  co_return co_await crtsys_ntl_rpc_sample::delayed_add_async(
      token, std::uint32_t{2000}, 40, 2);
}
```

C++ 표준은 coroutine handle과 suspend 규칙을 제공하지만 일반적인 `task<T>` 형식은
정의하지 않습니다. 따라서 `app/coroutine_task.hpp`는 최상위 예제 coroutine을
소유하는 작은 예제 전용 구현입니다. NTL이 제공하는 기능은 `async_call<T>`용
`co_await` adapter이며, 애플리케이션은 자체 coroutine task 또는 scheduler와 함께
사용할 수 있습니다. 완료는 Windows thread-pool wait로 감지하므로 RPC마다 blocking
thread 하나를 소비하지 않습니다.

stop 요청은 명시적 `cancel()`과 같은 transport 경로를 따릅니다.
`std::stop_token` -> `CancelIoEx` -> 서버 `call_context` 순입니다. suspend된
coroutine은 `ERROR_OPERATION_ABORTED`와 함께 한 번 재개됩니다. stop-token overload와
`<ntl/rpc/coroutine>`은 C++20 전용이며 기존 C++14 및 C++17 호출은 바뀌지 않습니다.

## 로드

평소 사용하는 격리된 드라이버 테스트 VM에서 실행하세요.

```bat
sc create CrtSysNtlRpcSample binpath= "C:\path\to\crtsys_ntl_rpc_sample.sys" type= kernel start= demand
sc start CrtSysNtlRpcSample
sc stop CrtSysNtlRpcSample
sc delete CrtSysNtlRpcSample
```

## 사용자 모드 RPC 앱

예제 앱은 동기, 비동기, 명시적 취소, coroutine 및 stop-token 파일을 순서대로
실행합니다.

```bat
examples\ntl-rpc-driver\build_x64\Debug\crtsys_ntl_rpc_sample_app.exe 21 7
```

대표 출력은 다음과 같습니다.

```text
sync: add=42 value=21 doubled=42 biased=28 server_irql=0 series=4
async: request started; caller remains available
async: completed with add=42
cancel: kernel callback observed cancellation
coroutine: suspended without blocking the caller
coroutine: resumed with add=42
stop_token: coroutine resumed with cancellation
reliable notification: sequence=1 text=reliable progress 42
stream: sequence=1 text=driver received: chunk 1
stream: sequence=2 text=driver received: chunk 2
stream: sequence=3 text=driver received: chunk 3
stream: completed
stream batch: sequence=1 text=driver received: batched chunk 1
stream batch: sequence=2 text=driver received: batched chunk 2
stream batch: sequence=3 text=driver received: batched chunk 3
all RPC examples completed
```
