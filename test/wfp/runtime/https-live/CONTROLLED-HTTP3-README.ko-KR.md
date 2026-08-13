# 제어된 HTTP/3 종단 간 허용성 검사

이 시험은 외부 인터넷과 브라우저 정책에 의존하지 않고 다음 실제 경로를
검증합니다.

```text
NTL 관리형 클라이언트 -- HTTP/3 + TLS 1.3 --> 검사 프록시
검사 프록시 -- HTTP/3 + TLS 1.3 --> 제어된 원본 서버
```

두 서버는 IPv4/IPv6 루프백에만 바인딩됩니다. 검사 CA와 원본 서버 CA는
프로세스가 소유하며 Windows 또는 브라우저 인증서 저장소에 설치하지
않습니다. leaf와 CA 개인 키는 종료할 때 삭제합니다. WFP 드라이버를
로드하지 않으며 재부팅도 요구하지 않습니다.

검증 범위는 다음과 같습니다.

- 양쪽 연결의 실제 msh3/MsQuic HTTP/3
- SNI, `:authority`, 인증서 DNS 이름 일치
- 검사 CA와 원본 서버 CA의 정확한 사설 CA 신뢰 및 잘못된 CA 거부
- identity, gzip, zlib `deflate`, Brotli HTML 검사
- 1 MiB 업스트림 응답 제한에서의 실패 시 차단 502
- 잘못된 제어 호스트에 대한 421
- 기본 8개 동시 요청과 정상 drain
- 루트 저장소, 임시 키, WFP 서비스의 전후 동일성

로컬 실행:

```powershell
.\Start-ControlledHttp3EndToEnd.ps1 `
  -PackageRoot . `
  -Concurrency 8
```

소스 트리에서 최소 패키지 준비:

```powershell
.\Prepare-ControlledHttp3Artifacts.ps1 `
  -Configuration Release `
  -PlatformToolset v145
```

VM 허용성 검사는 호스트에서 실행합니다. 암호는 명령줄 평문이 아니라
`SecureString`으로 전달합니다.

```powershell
$guestPassword = Read-Host -AsSecureString
$vmPassword = Read-Host -AsSecureString
.\Run-ControlledHttp3VmAcceptance.ps1 `
  -VmxPath 'D:\VMs\<test-vm>\<test-vm>.vmx' `
  -GuestPassword $guestPassword `
  -VmPassword $vmPassword
```

VM 테스트는 Driver Verifier 설정, 부팅 시각, 루트 저장소, 관련 키와
서비스, 충돌 이벤트와 새 덤프를 전후 비교합니다. VM을 재부팅하거나
드라이버를 설치하지 않습니다.
