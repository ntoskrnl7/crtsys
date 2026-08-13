# KMDF 컴파일 계약

이 대상은 장치가 없고 드라이버를 로드하지 않은 상태에서도 대표적인 `ntl::kmdf`
계약을 컴파일합니다. 이동 전용 요청/인터페이스 소유권, 콜백 시그니처, 핸들 너비
래퍼, 큐/지연 콜백, PnP 및 필터 전달, 자식/PDO, 인터럽트, DMA, USB, WMI 생성
경로를 검사합니다.

CI 진입점을 통해 지원하는 두 클라이언트/드라이버 아키텍처를 모두 빌드하십시오.

```powershell
.\scripts\ci\Build-CrtSys.ps1 `
  -Project kmdf-compile `
  -Architecture x64 `
  -Configuration Debug

.\scripts\ci\Build-CrtSys.ps1 `
  -Project kmdf-compile `
  -Architecture x86 `
  -Configuration Debug
```

이 대상은 `/W4 /WX`를 사용합니다. 공개 예제와 실제로 로드한 드라이버의 VM
테스트를 보완하는 검사이며 이를 대체하지는 않습니다.
