#include "../../../common/crt/crt_internal.h"

#pragma warning(disable : 4100)

extern "C" int __cdecl fflush(FILE *const stream) {
  // [현상]
  // libcntpr.lib에 fflush가 구현되어있어서, 구현을 하지 않으려 했으나
  // x86 환경에서 std::cout이 badbit가 설정되어 출력이 되지 않는 현상이
  // 발생하였습니다.
  //
  // [원인]
  // std::cin, std::cout등을 사용할때 sync 메서드가 내부적으로 호출되며
  // sync 메서드는 fflush를 호출하였습니다.
  //
  //    14.31.31103\include\fstream
  //    sync
  //    if (!_Myfile || _Traits::eq_int_type(_Traits::eof(), overflow()) ||
  //        0 <=_CSTD fflush(_Myfile)) {
  //        return 0;
  //    }
  //    return -1;
  //
  // 그러나 10.0.17763.0의 libcntpr.lib의 fflush를 호출항면 abort가
  // 호출되는 것을 확인하였습니다.
  // 하지만 abort는 void를 반환하기 때문에 rax/eax값이 0보다 크다면
  // sync함수는 -1을 반환하게 되며, 이로 인해서 std::cout에 badbit가 설정되여
  // std::cout으로 출력이 되지 않았습니다.
  //
  // eax에는 주소값이 저장되어있었으며, x86의 경우 주소의 최상위 비트가 1이 아니었기때문에
  // 이러한 현상이 발생하였으며, x64는 조스의 최상위 비트가 1이었기때문에 현상이 발생하지 않았었습니다.
  //
  // 이는 libcntpr.lib의 문제이므로 libcntpr.lib의 fflush를 사용하지 않고
  // 여기서 fflush를 정의하였습니다.
  //
  // 어차피 추후에는 fflush의 기능을 구현할 예정입니다.
  //

  // untested :-(
  return 0;
}