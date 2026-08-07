# KMDF 컴파일 contract

이 target은 장치가 없어도, driver를 load하지 않아도 대표적인 `ntl::kmdf` contract를 컴파일합니다. move-only request/interface 소유권, callback signature, handle 크기 facade, queue/지연 callback, PnP/filter forwarding, child/PDO, interrupt, DMA, USB, WMI 생성 경로를 검사합니다.

CI entry point를 통해 지원하는 두 client/driver architecture를 모두 빌드하십시오.

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

이 target은 `/W4 /WX`를 사용합니다. 공개 예제와 load된 driver VM 테스트를 보완하며 대체하지는 않습니다.
