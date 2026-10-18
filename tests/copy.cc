// SPDX-License-Identifier: Apache-2.0

#include "private/runtime.h"
#include <iostream>
int main(int argc, char **argv) {
  try {
    autoconf::Require(argc == 3, "copy SOURCE OUTPUT");
    std::filesystem::path output = argv[2];
    std::filesystem::create_directories(output.parent_path());
    std::filesystem::copy(
        argv[1], output,
        std::filesystem::copy_options::recursive |
            std::filesystem::copy_options::overwrite_existing);
    return 0;
  } catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}
