#include <exception>
#include <iostream>

int run_user_http3_service(int argc, wchar_t **argv);

int wmain(int argc, wchar_t **argv) {
  try {
    return run_user_http3_service(argc, argv);
  } catch (const std::exception &error) {
    std::cerr << "user HTTP/3 service failed: " << error.what() << '\n';
    return 1;
  }
}
