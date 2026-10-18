// SPDX-License-Identifier: Apache-2.0

#include "private/runtime.h"
#include <iostream>
int main(int argc, char **argv) {
  try {
    autoconf::Require(argc == 3, "usage: runner SPEC_JSON OUTPUT_TREE");
    autoconf::Run(autoconf::Parse(autoconf::Read(argv[1])), argv[2]);
    return 0;
  } catch (const std::exception &e) {
    std::cerr << "autoconf: " << e.what() << '\n';
    return 1;
  }
}
