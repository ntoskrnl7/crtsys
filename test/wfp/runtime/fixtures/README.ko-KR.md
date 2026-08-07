# WFP 런타임 fixture

이 디렉터리에는 WFP 런타임 acceptance에서 사용하는 제어된 트래픽 발생기와
결정적 origin이 있습니다. 대응하는 예제 controller 또는 policy service에는
이 코드를 넣지 않습니다.

경계는 다음과 같이 고정합니다.

- `examples/wfp/<runtime>/<sample>/driver`는 WFP callout 데이터 경로를
  소유합니다.
- 예제 controller 또는 policy service는 WFP 정책 수명, 드라이버 설정,
  제품 데이터 경로에 해당하는 redirect listener와 운영 telemetry를
  소유합니다.
- `test/wfp/runtime/fixtures/<runtime>/<sample>`은 controlled client/origin,
  비정상 트래픽, 부하 발생, 결과 검증과 PASS/FAIL marker를 소유합니다.
- fixture는 sibling controller/service를 실행하고 bounded ready/stop 규약으로
  제어할 수 있지만, WFP 정책을 직접 설치하거나 드라이버를 직접 설정하면
  안 됩니다.

실제 브라우저 검사 controller와 managed acceptance fixture는 서로 다른
실행 경로입니다. 실제 경로는 이미 실행 중인 브라우저의 경로를 애플리케이션
범위 지정에만 사용하며 브라우저를 실행·종료·재설정하거나 설정을 바꾸지
않습니다. 결정적 HTTP/1.1, HTTP/2, HTTP/3 검증용 client와 origin은 이곳에
두어 실제 브라우저 예제의 핵심 구조와 섞이지 않게 합니다.
