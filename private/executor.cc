// SPDX-License-Identifier: Apache-2.0

#include "private/runtime.h"
#include <iostream>
#include <unistd.h>
int main(int argc, char **argv) {
  try {
    using namespace autoconf;
    Require(
        argc >= 3,
        "usage: executor write OUTPUT CHUNKS... | exec ENV_JSON TOOL ARGS...");
    if (std::string(argv[1]) == "write") {
      std::string text;
      for (int i = 3; i < argc; ++i)
        text += argv[i];
      Write(argv[2], text);
    } else if (std::string(argv[1]) == "header") {
      Require(argc == 4 && argv[3][0] == '@',
              "header requires a multiline parameter file");
      Write(argv[2], "#pragma once\n" + Read(std::string(argv[3] + 1)));
    } else {
      Require(std::string(argv[1]) == "exec" && argc >= 4,
              "invalid executor mode");
      auto env = Env(Parse(argv[2]));
      std::vector<std::string> vars;
      std::vector<char *> pointers;
      for (const auto &[k, v] : env)
        vars.push_back(k + '=' + v);
      for (auto &v : vars)
        pointers.push_back(v.data());
      pointers.push_back(nullptr);
      std::string tool = argv[3];
      if (tool.find('/') == std::string::npos)
        tool = "./" + tool;
      argv[3] = tool.data();
      execve(tool.c_str(), argv + 3, pointers.data());
      throw std::runtime_error("cannot execute: " + tool);
    }
    return 0;
  } catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}
