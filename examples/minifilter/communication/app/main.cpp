#include <iostream>

#include "communication.hpp"

int wmain() {
  if (!crtsys_minifilter_communication_sample::run_communication_sample())
    return 1;

  std::cout << "NTL minifilter communication sample completed\n";
  return 0;
}
