# NTL 유니코드 문자열

[NTL 문서로 돌아가기](./README.ko-KR.md)

헤더: [`include/ntl/unicode_string`](../../include/ntl/unicode_string)

`ntl::unicode_string`은 `std::wstring` 저장소를 `UNICODE_STRING`에 맞게 조정합니다. 드라이버 설정 코드가 C++ 문자열 유틸리티로 이름을 만든 뒤 `PUNICODE_STRING`을 요구하는 WDK API를 호출할 때 유용합니다.

API:

- `std::wstring`으로 생성
- `c_str() const`
- `operator*()`

예제:

```cpp
std::wstring name = L"\\Device\\demo";
ntl::unicode_string native_name{name};

OBJECT_ATTRIBUTES attributes;
InitializeObjectAttributes(&attributes,
                           &*native_name,
                           OBJ_KERNEL_HANDLE,
                           nullptr,
                           nullptr);
```

`UNICODE_STRING` 뷰는 `ntl::unicode_string` 객체 내부의 C++ 문자열 저장소를 빌립니다. 네이티브 문자열 뷰가 사용되는 동안 객체를 계속 유지하십시오.

IRQL: `std::wstring`을 통한 생성은 `PASSIVE_LEVEL`로 취급해야 합니다. 접근자는 가볍지만 수명과 저장소는 여전히 이를 소유한 C++ 객체에 속합니다.
