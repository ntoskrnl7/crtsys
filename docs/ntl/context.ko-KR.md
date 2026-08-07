# NTL 실행 문맥과 IRQL

[NTL 문서로 돌아가기](./README.ko-KR.md)

NTL은 주로 드라이버 제어 경로를 위해 설계되었습니다. API에 더 넓은 계약이 명시되어 있지 않다면 `PASSIVE_LEVEL`로 간주하십시오.

NTL 문서에서는 보수적인 계약을 사용합니다.

- `PASSIVE_LEVEL`은 도우미가 메모리를 할당하거나, 대기하거나, 예외를 던지거나, pageable 코드를 사용하거나, 런타임/STL 상태에 의존할 수 있음을 뜻합니다.
- `<= APC_LEVEL`은 도우미가 APC 수준 호출자를 허용하는 WDK 프리미티브를 사용할 수 있지만 DPC/ISR에는 안전하지 않음을 뜻합니다.
- `<= DISPATCH_LEVEL`은 다른 전제 조건이 충족될 때 dispatch 수준에서 사용할 수 있는 WDK 프리미티브를 중심으로 의도적으로 작성되었음을 뜻합니다.

드라이버 테스트 모음의 광범위한 C++/CRT/STL 매트릭스는 여전히 `PASSIVE_LEVEL`에서 실행됩니다. 더 높은 IRQL에서의 사용은 해당 연산에 대해 별도로 설계 판단을 내려야 합니다.

## 실용적인 해석 규칙

NTL 도우미가 C++ 객체를 소유하거나, 콜백을 저장하거나, STL 컨테이너를 사용하거나, 예외를 던질 수 있거나, 메모리를 할당할 수 있다면 `PASSIVE_LEVEL`로 취급하십시오.

주석으로만 계약을 남기는 대신 드라이버가 `NTSTATUS`를 보고해야 한다면 API 경계에서 `ntl::require_passive_level()` 또는 `ntl::require_irql_at_most()`를 사용하십시오.

NTL 도우미가 WDK 프리미티브의 얇은 래퍼라면 해당 프리미티브의 네이티브 계약과 주제별 문서의 추가 설명을 따르십시오.

예:

- `ntl::driver::on_unload`는 C++ 콜백을 저장하는 초기화/해제 도우미이므로 `PASSIVE_LEVEL`입니다.
- `ntl::spin_lock::lock_at_dpc_level`은 스핀 록 경로에 대응하며 호출자가 이미 `DISPATCH_LEVEL`에 있어야 합니다.
- `ntl::work_item::queue`는 WDK 작업 항목 큐 연산에 대응하므로 `<= DISPATCH_LEVEL`에서 호출할 수 있지만, `wait()`와 런타임 사용이 많은 콜백의 소유권은 계속 `PASSIVE_LEVEL` 문제로 다뤄야 합니다.
- `ntl::allocate_pool(..., ntl::pool_kind::nonpaged, ...)`의 원시 할당에는 WDK 풀 규칙을 적용할 수 있지만, 이 할당자를 사용하는 `std::vector`는 여전히 C++ 컨테이너이므로 별도 검토가 없다면 `PASSIVE_LEVEL`로 취급해야 합니다.

프로젝트 수준의 실행 모델은 [설계 근거와 운영 경계](../design-rationale.ko-KR.md)를 참고하십시오.
