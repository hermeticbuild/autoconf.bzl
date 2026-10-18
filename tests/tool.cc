// SPDX-License-Identifier: Apache-2.0

#include <chrono>
#include <csignal>
#include <cstdlib>
#include <iostream>
#include <string>
#include <thread>
int main(int argc, char **argv) {
  if (argc < 2)
    return 127;
  std::string mode = argv[1];
  if (mode == "scalar") {
    std::cout << argv[2] << '\n';
    return 0;
  }
  if (mode == "exit")
    return std::stoi(argv[2]);
  if (mode == "signal") {
    std::raise(SIGTERM);
    return 0;
  }
  if (mode == "timeout") {
    std::this_thread::sleep_for(std::chrono::seconds(10));
    return 0;
  }
  if (mode == "environment")
    return std::getenv("AUTOCONF_UNDECLARED") ? 2 : 0;
  return 127;
}
