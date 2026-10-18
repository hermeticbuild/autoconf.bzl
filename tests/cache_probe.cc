// SPDX-License-Identifier: Apache-2.0

#include "private/runtime.h"
#include <iostream>
int main(int argc, char **argv) {
  try {
    std::cout << autoconf::Read(autoconf::Runfile(argv[0], "cache_value"));
    return 0;
  } catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}
