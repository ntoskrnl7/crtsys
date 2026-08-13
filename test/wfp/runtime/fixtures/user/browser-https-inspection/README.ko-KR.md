# 브라우저 HTTPS 검사 트래픽 픽스처

[English](./README.md)

이 디렉터리에는 `examples/wfp/user/browser-https-inspection`의 결정적 트래픽
생성과 허용성 검사 전용 코드만 있습니다. WFP 정책 세션을 소유하거나
드라이버를 설정하거나 `DeviceIoControl`을 호출하지 않습니다.

제품 측 실행 파일은 다음처럼 분리됩니다.

- `crtsys_wfp_browser_https_inspection_controller.exe`는 이미 실행 중인
  브라우저를 관찰하고 TCP 브라우저 정책 수명을 소유합니다.
- `crtsys_wfp_browser_https_inspection_http3_proxy_service.exe`는 H3 엔드포인트를
  소유하며 `--wfp-managed-http3-proxy` 모드에서는 앱 범위 양방향 UDP/443 튜플 변환
  정책도 직접 소유합니다.

픽스처 대상은 다음과 같습니다.

- `crtsys_wfp_browser_https_inspection_acceptance`: 전용 루프백 H3 원본 서버,
  검사 프록시, 트래픽, 제한된 프로토콜 검증
- `crtsys_wfp_browser_https_inspection_managed_client_acceptance`: 트래픽만
  생성하는 관리형 H3 클라이언트
- `*_msh3_client_acceptance`, `*_raw_msquic_acceptance`,
  `*_msquic_loopback_acceptance`: 전송 계약 실행 파일

별도 프로젝트로 빌드합니다.

```powershell
cmake -S test\wfp\runtime\fixtures\user\browser-https-inspection `
      -B artifacts\test\wfp-browser-https-inspection-acceptance -A x64
cmake --build artifacts\test\wfp-browser-https-inspection-acceptance `
      --config Release
```

루프백 실행 파일은 허용성 검사 트래픽 모드만 받습니다.

```text
crtsys_wfp_browser_https_inspection_acceptance.exe --controlled-http3-e2e <proxy-port> <origin-port> <log-directory> [duration-seconds]
```

`Start-ManagedHttp3Acceptance.ps1`은 제품 H3 프록시 서비스를 자식 프로세스로
실행하고 `<log-directory>/service.ready`를 기다린 뒤 관리형 클라이언트 트래픽을
생성합니다. 끝나면 `<log-directory>/stop.request`를 만들어 drain 종료를
요청합니다. WFP 정책을 설치하고 제거하는 주체는 픽스처가 아니라 서비스
프로세스입니다.

공용 런타임 실행기와 패키징 매니페스트는 이 픽스처 밖의 런타임
허용성 검사 계층에서 별도로 관리합니다.
