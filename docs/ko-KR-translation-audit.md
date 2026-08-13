# 한국어 문서 번역 감사 목록

이 문서는 한국어 문서의 번역 품질을 추적하기 위한 작업 목록이다. 번역 본문을
수정하지 않으며, 영문 원문과 비교할 때 확인하거나 개선할 사항만 기록한다.

## 범위와 판정 기준

- 대상: Git에서 추적하는 `*.ko-KR.md` 132개
- 영문 원문이 있는 문서는 해당 `.md`와 비교한다.
- 영문 짝이 없는 문서는 한국어 문서 자체의 구조와 용어만 점검한다.
- 코드 블록, API/형식 이름, 명령, 경로는 원문 그대로 보존하는 것이 원칙이다.
- 다음은 단독으로 오류 확정 신호가 아니다: 한국어 문서의 짧은 분량, 의도적인
  한국어 전용 설명, 용어의 영문 병기.

### 용어 기준

- `facade`는 문맥에 따라 **래퍼**, **고수준 API**, **소유 객체** 등으로 풀어 쓴다.
  `파사드`를 기계적으로 음역하지 않는다.
- `origin security`는 이 저장소의 TLS 문맥에서 **서버 신원 검증**으로 쓴다. 인증서
  체인, 핀, 클라이언트 인증서, ALPN 확인을 묶어 설명할 때 특히 적절하다.
- `typed`는 무조건 `형식화된`으로 옮기지 않는다. 형식 자체라면 **구조화된**,
  타입 시스템 계약이라면 **타입 안전한** 또는 **타입이 지정된**으로 문맥에 맞춘다.
- `destroy`는 메모리 파괴가 아니라 C++ 객체의 수명 종료를 뜻하면 **소멸시키다**를
  우선한다.

## 이미 전면 대조·수정한 문서 (22개)

아래 문서는 이번 감사에서 재번역 대상으로 넣지 않는다. 다만 이후 원문이 바뀌면
다른 번역과 마찬가지로 다시 비교해야 한다.

- `docs/ntl/{README,async-socket,driver-device-irp,file-object,io-buffer-implementation-checklist,io-buffer-mapping,ipc,kmdf-driver-checklist,kmdf,minifilter,network-dual-runtime,protocol-inspection,rpc,tls-stream,wfp-guide,wfp}.ko-KR.md`
- `examples/kmdf/basic/README.ko-KR.md`
- `examples/wfp/kernel/browser-https-inspection/README.ko-KR.md`
- `examples/wfp/user/http3-inspection/README.ko-KR.md`
- `test/flt/runtime/README.ko-KR.md`
- `test/kmdf/{compile,runtime}/README.ko-KR.md`

## P1 — 원문 기준으로 새 번역 또는 대폭 정리가 필요한 문서

| 문서 | 근거 | 개선 방향 |
| --- | --- | --- |
| `docs/ntl-api.ko-KR.md` — **완료** | 영문은 주제별 API 색인인데 한국어판은 별도의 오래된 상세 API 문서다. 제목·링크·글머리 구조가 크게 다르며, 원문에 없는 상세 계약도 섞여 있었다. | 영문 색인을 기준으로 다시 작성하고, 상세 설명은 해당 `docs/ntl/*.ko-KR.md`로 연결했다. 오래된 API 목록은 제거했다. |
| `docs/ntl/pool-allocator.ko-KR.md` — **완료** | 영문과 글머리 목록이 크게 달라 누락 또는 구조 붕괴 가능성이 높았다. 메모리 할당·IRQL 문서라 의미 오역 위험도 높았다. | 풀 종류, 태그, IRQL, STL/PMR, 소유권 규칙을 영문 순서대로 전면 재번역했다. |
| `examples/wfp/user/browser-https-inspection/README.ko-KR.md` — **완료** | 제목/목록 수와 본문 분량이 영문에서 크게 벗어났다. TLS·브라우저·fallback 보안 설명은 누락되면 위험했다. | 실행 경계, 브라우저 비변경 원칙, 인증서/핀/HTTP2·HTTP3 fallback, acceptance 책임을 영문 기준으로 대조하고 누락된 규칙 평가 순서·HPACK 상태·변환 경계·mTLS 실패 조건을 보완했다. |

## P2 — 정밀 대조·개선 완료 문서

| 문서 | 확인할 내용 |
| --- | --- |
| `docs/feature-coverage.ko-KR.md` | 영문 목록과 항목 수가 다르다. 지원 여부, 제외 범위, 검증 상태가 빠지거나 과장되지 않았는지 대조한다. |
| `README.ko-KR.md` | 영문 링크보다 하나 더 많고 본문이 짧다. 프로젝트 범위, 빌드/검증 경로, 문서 색인의 누락을 확인한다. |
| `nuget/README.ko-KR.md` — **완료** | NuGet 패키지 구성·복원·소비자 절차의 목록 누락과 문장 붕괴가 있었다. | 패키지 내용, CI 검증, MsQuic ABI, 미니필터 대상 버전, 릴리스 아티팩트 설명을 영문 기준으로 정리했다. |
| `vcpkg/README.ko-KR.md` — **완료** | 포트 사용법, feature, triplet, 설치/검증 절차의 누락 가능성이 있었다. | `content-codecs`와 `msquic-headers` 기능을 분리하고, 소비자 구성 중 다운로드 금지와 계약/전체 검증 절차를 영문 기준으로 보완했다. |
| `examples/wfp/README.ko-KR.md` — **완료** | 예제 색인 링크 수가 영문과 달랐다. | 영문과 대조한 결과 한국어 안내 링크로 바꾼 의도적인 차이였으며, 커널/사용자 모드 대응·보안 경계·빌드 설명은 모두 포함되어 있음을 확인했다. |
| `examples/wfp/user/tcp-content-filter/README.ko-KR.md` — **완료** | `typed`를 `형식화된`으로 일관되게 번역한 부분이 있었다. | 문맥에 따라 `구조화된` 및 `타입이 지정된`으로 바로잡고, RPC 판정·fail-closed·acceptance 책임을 원문과 대조했다. |
| `examples/wfp/user/udp-content-filter/README.ko-KR.md` — **완료** | `typed`를 `형식화된`으로 번역한 부분이 있었다. | `구조화된`으로 바로잡고, clone/reinjection 소유권·self-injection·실패 시 차단 의미를 원문과 대조했다. |

## P3 — 영문 대조가 필요한 기술 문서

자동 구조 검사는 큰 누락을 확정하지 못했다. 그러나 기존 기계번역의 품질 이력상
아래 문서는 문단 단위 영문 대조를 완료했다. 변경하지 않은 문서는 원문 의미와
구조를 보존함을 확인한 경우다.

### 완료한 P3 대조

- `docs/ntl/context.ko-KR.md` — 원문과 대조 완료. 의미·구조·코드 예제가 일치한다.
- `docs/ntl/coroutine.ko-KR.md` — 원문과 대조 완료. 수명·언로드·실패 문맥의 의미가 보존된다.
- `docs/ntl/device-control-pattern.ko-KR.md` — 원문과 대조 완료. `typed IOCTL`을 `타입이 지정된 IOCTL`로 바로잡았다.
- `docs/ntl/device-interface.ko-KR.md` — 원문과 대조 완료. PnP PDO 조건과 인터페이스 수명 규칙이 보존된다.
- `docs/ntl/event.ko-KR.md` — 원문과 대조 완료. `KEVENT`의 대기 IRQL 조건과 신호 소비 의미가 보존된다.
- `docs/ntl/ioctl.ko-KR.md` — 원문과 대조 완료. `typed` 관련 번역을 `타입이 지정된`으로 바로잡았다.
- `docs/ntl/result.ko-KR.md` — 원문과 대조 완료. `ntl::result<T>`의 값/상태 및 IRQL 계약이 보존된다.
- `docs/ntl/remove-lock.ko-KR.md` — **재작성 완료**. 문자 인코딩이 손상되어 있어 영문 원문 기준으로 전체를 다시 작성했다.
- `docs/ntl/system-thread.ko-KR.md` — 원문과 대조 완료. handle 소유권과 언로드 전 join 규칙이 보존된다.
- `docs/ntl/timer.ko-KR.md` — 원문과 대조 완료. DPC 종료 동기화와 `DISPATCH_LEVEL` 제약이 보존된다.
- `docs/ntl/wait.ko-KR.md` — 원문과 대조 완료. 0 timeout 대기의 신호 소비 규칙이 보존된다.
- `docs/ntl/symbolic-link.ko-KR.md` — 원문과 대조 완료. 심볼릭 링크의 소유권·정리 순서와 IRQL 계약이 보존된다.
- `docs/ntl/mdl.ko-KR.md` — 원문과 대조 완료. MDL 해제·페이지 잠금·매핑의 수명 규칙이 보존된다.
- `docs/ntl/lookaside-list.ko-KR.md` — 원문과 대조 완료. 룩어사이드 리스트와 RAII 포인터의 수명·IRQL 규칙이 보존된다.
- `docs/ntl/ownership.ko-KR.md` — **재작성 완료**. 문자 인코딩이 손상되어 있어 사용자/커널 핸들과 객체 참조의 소유권 계약을 영문 원문 기준으로 다시 작성했다.
- `docs/ntl/passive-executor.ko-KR.md` — **재작성 완료**. 문자 인코딩이 손상되어 있어 PASSIVE_LEVEL 실행·DPC 전달·언로드 drain 계약을 영문 원문 기준으로 다시 작성했다.
- `docs/ntl/registry.ko-KR.md` — **재작성 완료**. 문자 인코딩이 손상되어 있어 레지스트리 키·값 형식·IRQL·소유권 계약을 영문 원문 기준으로 다시 작성했다.
- `docs/ntl/synchronization.ko-KR.md` — **재작성 완료**. 문자 인코딩이 손상되어 있어 IRQL·스핀 잠금·ERESOURCE 계약을 영문 원문 기준으로 다시 작성했다.
- `docs/ntl/status-exceptions-stack.ko-KR.md` — **재작성 완료**. 문자 인코딩이 손상되어 있어 상태·SEH 경계·스택 확장·IRQL 계약을 영문 원문 기준으로 다시 작성했다.
- `docs/ntl/unicode-string.ko-KR.md` — **재작성 완료**. 문자 인코딩이 손상되어 있어 `UNICODE_STRING` 뷰의 저장소·수명·IRQL 규칙을 영문 원문 기준으로 다시 작성했다.
- `docs/ntl/work-item.ko-KR.md` — **재작성 완료**. 문자 인코딩이 손상되어 있어 work item 큐잉·대기·소멸·캡처 상태의 수명 계약을 영문 원문 기준으로 다시 작성했다.
- `docs/ntl/inspection.ko-KR.md` — 원문과 대조 완료. `typed verdict`를 `구조화된 판정`으로 바로잡고, 손상된 문장을 복원했다.
- `docs/ntl/io-buffer-implementation-checklist.ko-KR.md` — 원문과 대조 중 minifilter pre/post 작업·매핑·교체 계약의 `typed` 번역을 바로잡았다.
- `docs/ntl/io-buffer-mapping.ko-KR.md` — 원문과 대조 중 pre/post 작업, `copy_back()`, 컨텍스트 소유자, 사용자 서비스 설명자의 `typed` 번역을 바로잡았다.
- `examples/minifilter/basic/README.ko-KR.md` — **재작성 완료**. 문자 인코딩이 손상되어 있어 콜백·stream context·이름 스냅샷·고도 규칙을 영문 원문 기준으로 다시 작성했다.
- `examples/minifilter/communication/README.ko-KR.md` — **재작성 완료**. 문자 인코딩이 손상되어 있어 통신 포트·RPC·공유 메모리 ring·설치 절차를 영문 원문 기준으로 다시 작성했다.
- `examples/minifilter/control-device/README.ko-KR.md` — **재작성 완료**. 문자 인코딩이 손상되어 있어 control device·언로드 거부·관리자 권한·검증 fixture 규칙을 영문 원문 기준으로 다시 작성했다.
- `examples/minifilter/operation-log/README.ko-KR.md` — 원문과 대조 완료. MiniSpy 대응 범위와 cleanup/close 시점 차이를 보존하고 `typed` 번역을 바로잡았다.
- `examples/minifilter/swap-buffers/README.ko-KR.md` — 원문과 대조 완료. IOPB 교체 전 원본 버퍼 잠금 금지, fail-closed 이름 해석, MDL·SEH 수명 규칙과 의도적 제한을 보존한다.
- `examples/minifilter/volume-metadata/README.ko-KR.md` — 원문과 대조 완료. 볼륨 lock/dismount 재열기와 resident context 수명 규칙을 보존하고 `typed` 번역을 바로잡았다.
- `examples/minifilter/README.ko-KR.md` — 원문과 대조 완료. 예제와 runtime fixture의 역할 분리 및 WDK 대응 범위를 보존하고 `typed` 번역을 바로잡았다.
- `examples/wfp/kernel/browser-https-inspection/README.ko-KR.md` — 원문과 대조 완료. `origin security`를 의미가 분명한 `서버 신원 검증`으로 통일했다.
- `examples/ntl-driver/README.ko-KR.md` — 원문과 대조 완료. IOCTL 계약·레지스트리·PASSIVE 작업 경계가 보존되며 `typed CTL_CODE`를 바로잡았다.
- `examples/ntl-rpc-driver/README.ko-KR.md` — 원문과 대조 완료. RPC 수명·권한 검사·취소·알림·streaming 계약을 보존하고 `typed` 번역을 바로잡았다.
- `examples/kmdf/bus/README.ko-KR.md` — 원문과 대조 완료. `typed` 관련 번역을 `타입이 지정된`으로 바로잡았다.
- `examples/kmdf/dma/README.ko-KR.md` — 원문과 대조 완료. DMA DPC/ISR의 IRQL 제한과 lock-free 요구가 보존된다.
- `examples/kmdf/usb/README.ko-KR.md` — 원문과 대조 완료. continuous reader의 IRQL·하드웨어 적용 조건이 보존된다.
- `examples/kmdf/wmi/README.ko-KR.md` — 원문과 대조 완료. WMI 콜백 IRQL과 execution level 제약이 보존된다.
- `examples/kmdf/echo/README.ko-KR.md` — 원문과 대조 완료. 동기/비동기 echo 경로와 취소 경합의 검증 범위가 보존된다.
- `examples/kmdf/filter-stack/README.ko-KR.md` — 원문과 대조 완료. 요청 전달·완료 콜백·계층별 변환 계약을 보존하고, `typed` 번역을 바로잡았다.
- `examples/kmdf/pnp/README.ko-KR.md` — 원문과 대조 완료. PnP/power 수명 주기와 사용자 모드 검증 절차를 보존하고, `typed` 번역을 바로잡았다.
- `examples/kmdf/reference/README.ko-KR.md` — 원문과 대조 완료. 버퍼 별칭·클래스별 동작·단일 실행 검증 절차가 보존된다.
- `test/rpc/async/README.ko-KR.md` — 원문과 대조 완료. 취소·drain·재시작 계약이 보존된다.
- `test/rpc/lifecycle-stress/README.ko-KR.md` — 원문과 대조 완료. 수명 경계와 반복 중지/재시작 절차가 보존된다.
- `test/rpc/notifications/README.ko-KR.md` — 원문과 대조 완료. 타입이 지정된 알림·ACK·세션 종료 규칙이 보존된다.
- `test/rpc/security/README.ko-KR.md` — 원문과 대조 완료. 보안 context와 인증/권한 부여의 구분이 보존된다.
- `test/rpc/streaming/README.ko-KR.md` — 원문과 대조 완료. 스트림 ACK·취소·재연결 규칙이 보존된다.
- `test/net/kernel-contracts/README.ko-KR.md` — 원문과 대조 완료. 커널 workspace·Verifier·런타임 정리 계약이 보존되며 `destruction-order`를 `소멸 순서`로 바로잡았다.
- `test/{flt,kmdf,wfp}/WDK-SAMPLE-COVERAGE.ko-KR.md` — 원문과 대조 중 `typed`의 기계적 번역을 타입 계약에는 `타입이 지정된`, 판정·정책 데이터에는 `구조화된`으로 바로잡았다.
- `test/nuget/README.ko-KR.md` — 원문과 대조 완료. NuGet 소비자 스모크 테스트의 대상 구성, props/targets, 서명 제외 범위를 자연스러운 기술 한국어로 정리했다.
- `test/rpc/{cross-bitness,security}/README.ko-KR.md` — 원문과 대조 완료. 포인터 폭 독립 직렬화와 호출자 보안 컨텍스트·권한 확인 계약을 보존하고 32/64비트·보안 용어를 정리했다.
- `test/flt/verifier-stress/README.ko-KR.md` — 원문과 대조 완료. 포트 정리 후 언로드/재로드를 검사하는 목적과 Verifier 설정 불변 조건을 정리했다.
- `test/wfp/runtime/fixtures/{README,kernel/ale-connect-block,kernel/async-inspection,kernel/connect-redirect,kernel/tls-inspection-proxy}/README.ko-KR.md` — 원문과 대조 완료. 픽스처와 제품 컨트롤러의 책임 경계를 보존하고 acceptance·origin·fixture 등의 기계적 영어 나열을 정리했다.
- `test/wfp/runtime/fixtures/user/{browser-https-inspection,connect-redirect,http3-inspection,tls-inspection-proxy}/README.ko-KR.md` — 원문과 대조 완료. 제품 서비스와 트래픽 픽스처의 분리, 실패 시 차단, 수명 주기 IPC 경계를 보존하고 번역투를 정리했다.
- `test/wfp/runtime/fixtures/kernel/http3-inspection/README.ko-KR.md` — 원문과 대조 완료. 커널 HTTP/3 제품 경로와 트래픽 픽스처의 분리를 보존하고 번역투를 정리했다.
- `test/wfp/runtime/https-live/README.ko-KR.md` — **재작성 완료**. 문장 붕괴가 남아 있어 영문 원문 기준으로 라이브 HTTPS·브라우저 검사, 관리형 HTTP/3, 보안 경계를 전체 재번역했다.
- `test/wfp/runtime/{README,https-live/CONTROLLED-HTTP3-README}.ko-KR.md` — 원문 또는 한국어 전용 제어 H3 문서를 대조·점검했다. 런타임 게이트, 일회용 VM, 인증서·브라우저 변경 금지 경계를 정리했다.

### 최상위·설계·배포 문서

- `README.ko-KR.md` — 원문과 대조 완료. 제목·코드 블록·링크 구조가 일치하며, 프로젝트 개요와 빌드·검증 경계에 의미 누락이 없다.
- `docs/README.ko-KR.md` — 영문 짝 없음. 한국어 색인과 로컬 링크를 점검했고, 깨진 링크가 없다.
- `docs/architecture.ko-KR.md` — 원문과 대조 완료. 런타임 기반 계층, 다중 드라이버 TLS 상태, 드라이버 모델 책임을 자연스러운 기술 한국어로 정리했다.
- `docs/usage-examples.ko-KR.md` — 원문과 대조 완료. 앱/드라이버 경계와 RPC 스키마·콜백 설명의 기계적 영어 나열을 정리했다.
- `docs/design-rationale.ko-KR.md` — 원문과 대조 완료. 런타임의 목표 범위, IRQL·동기화·예외·스택 경계를 자연스러운 기술 한국어로 정리했다.
- `docs/msbuild-nuget-quickstart.ko-KR.md` — 원문과 대조 완료. NuGet 소비 경로, 복원, 드라이버 모델 선택의 용어를 정리했다.
- `docs/ci-driver-load-tests.ko-KR.md` — 원문과 대조 완료. 자체 호스팅 러너, 테스트 서명, 아키텍처 제약과 로드 테스트 순서가 보존된다.
- `examples/{kmdf,minifilter}/README.ko-KR.md`
- `examples/{ntl-driver,ntl-rpc-driver}/README.ko-KR.md`
- `test/{flt,kmdf}/README.ko-KR.md`
- `test/{flt,kmdf,wfp}/WDK-SAMPLE-COVERAGE.ko-KR.md`
- `test/nuget/README.ko-KR.md`

### NTL API 문서

- `docs/ntl/{context,coroutine,device-control-pattern,device-interface,event,inspection,ioctl,lookaside-list,mdl,ownership,passive-executor,registry,remove-lock,result,status-exceptions-stack,symbolic-link,synchronization,system-thread,timer,unicode-string,wait,work-item}.ko-KR.md`

특히 `inspection`, `ownership`, `registry`, `lookaside-list`, `result`는 분량 차이가
커서 누락 여부부터 확인한다. IRQL·소유권·동시성·예외 관련 문장은 축약 번역을
허용하지 않는다.

### KMDF·미니필터 예제

- `examples/kmdf/{bus,dma,echo,filter-stack,pnp,reference,usb,wmi}/README.ko-KR.md` — 원문과 대조 완료. IRQL, DMA, PnP, WMI, 버퍼 수명 및 검증 절차가 보존된다.
- `examples/minifilter/{basic,communication,control-device,operation-log,swap-buffers,volume-metadata}/README.ko-KR.md` — 원문과 대조 완료. pre/post 작업, 버퍼 교체, 통신·수명 경계를 보존한다.
- `test/kmdf/{verifier-stress}/README.ko-KR.md`, `test/flt/{verifier-stress}/README.ko-KR.md`, `test/flt/runtime/{CDO,DELETE,IO-BUFFER,METADATA,NAME-CHANGER,SCANNER,SIMREP}-README.ko-KR.md` — 원문과 대조 완료. Verifier·언로드·I/O 버퍼 수명 계약을 보존한다.

### WFP·네트워크 예제와 테스트

- `examples/wfp/kernel/{ale-connect-block,async-inspection,bind-redirect,connect-redirect}/README.ko-KR.md` — 원문과 대조 완료. 컨트롤러와 픽스처의 책임 경계, ALE 정책, 실패 시 차단 및 수명 주기 IPC 설명을 정리했다.
- `examples/wfp/kernel/{datagram-proxy,flow-monitor,http3-inspection,specialized-observation,stream-edit,tls-inspection-proxy}/README.ko-KR.md` — 원문과 대조 완료. WFP 계층·콜아웃·튜플·제한된 리소스 계약을 보존하고 기계적 영어 나열을 정리했다.
- `examples/wfp/user/{connect-redirect,tls-inspection-proxy}/README.ko-KR.md` — 원문과 대조 완료. 프록시 서비스와 허용성 검사 픽스처의 책임 분리, 실패 시 차단, 인증서·TLS 경계를 정리했다.
- `examples/wfp/README.ko-KR.md`, `examples/wfp/kernel/{bind-redirect,tcp-content-filter,udp-content-filter}/README.ko-KR.md` — 원문과 대조 완료. WFP 계층·리디렉션·실패 시 차단 경계가 보존된다.
- `test/wfp/{compile}/README.ko-KR.md`, `test/wfp/runtime/{README,advanced,ale-connect-block}/README.ko-KR.md` — 원문과 대조 완료. VM 변경 승인·Verifier 불변·정책 복구 계약이 보존된다.
- `test/wfp/runtime/fixtures/README.ko-KR.md`, `test/wfp/runtime/fixtures/kernel/{ale-connect-block,async-inspection,connect-redirect,http3-inspection,tls-inspection-proxy}/README.ko-KR.md`, `test/wfp/runtime/fixtures/user/{browser-https-inspection,connect-redirect,http3-inspection,tls-inspection-proxy}/README.ko-KR.md` — 원문과 대조 완료. 제품과 픽스처의 책임 분리가 보존된다.
- `test/wfp/runtime/https-live/{README,CONTROLLED-HTTP3-README}.ko-KR.md` — 검토 완료. 테스트 제어 조건과 인증서·브라우저·VM 보안 경계를 확인했다.

### RPC·기타 테스트

- `test/rpc/{async,cross-bitness,lifecycle-stress,notifications,security,streaming}/README.ko-KR.md` — 원문과 대조 완료. 취소·재시작·세션·호출자 보안 계약이 보존된다.
- `test/net/kernel-contracts/README.ko-KR.md`

## P4 — 법적 고지

- `docs/cppreference-attribution.ko-KR.md`
- `docs/third-party-notices.ko-KR.md`

법적 문구는 자연스러운 의역보다 원문 충실성이 우선이다. 라이선스명, 저작권 표시,
URL, 인용문은 임의로 다듬지 말고 영문과 정확히 대조한다.

## 완료 기준

각 P1~P3 문서를 처리할 때 다음을 모두 만족하면 이 목록에서 완료로 바꾼다.

1. 영문 원문의 제목, 문단, 목록, 표, 링크와 코드 블록을 대조한다.
2. 누락·추가·의미 반전·번역투·용어 불일치를 항목별로 기록한다.
3. 번역을 수정한 뒤 코드/명령/식별자와 로컬 링크를 검증한다.
4. 보안, 수명, 취소, IRQL, 권한 확인 문장은 축약하지 않고 원문 의미를 보존한다.

## 전수 기계 검증 결과

- Git 추적 한국어 Markdown 132개를 대상으로, 영문 짝이 있는 문서의 제목 수와 코드
  펜스 수를 비교했다. 불일치는 한국어판의 의도적인 보충 설명이 있는
  `docs/ntl/wfp-guide.ko-KR.md`와
  `examples/wfp/user/browser-https-inspection/README.ko-KR.md`뿐이며, 보충 섹션을
  별도로 확인했다.
- 코드 펜스 수는 `docs/ntl/wfp-guide.ko-KR.md`의 의도적인 한국어 보충 블록을
  제외하고 영문 짝과 일치한다. 실행 코드·명령·식별자 차이는 없으며, 내용 차이는
  Mermaid/`text` 블록의 한국어 현지화 또는 한국어 경로 자리표시자에 한정된다.
- 코드 펜스와 인라인 코드를 제외한 Markdown 링크를 전수 검사했다. 깨진 로컬 링크는
  없다. `docs/ntl/rpc.ko-KR.md`의 `` `[](args...)` `` 표기는 링크가 아닌 콜백
  표기이므로 검사에서 제외한다.
- `git diff --check`를 실행해 공백 오류가 없음을 확인했다. Git의 LF/CRLF 경고는
  오류가 아니라 작업 트리 줄바꿈 변환 경고다.
- `형식화된`의 전수 용례를 다시 확인했다. 남은 `docs/README.ko-KR.md`의
  `std::format`/`std::print` 설명은 문자열 formatting을 뜻하는 올바른 용례이며,
  타입·구조 데이터를 뜻하는 기계적 번역은 제거했다.

## 법적 고지 대조

- `docs/cppreference-attribution.ko-KR.md` — 인용 출처 URL과 예제 목록을 보존한다.
- `docs/third-party-notices.ko-KR.md` — 라이선스 원문, 저작권 표시, 저장소와 고정
  리비전을 보존한다. 법적 조항은 의도적으로 번역하지 않으며, 원문 보존을 확인했다.
