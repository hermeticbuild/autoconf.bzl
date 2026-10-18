// SPDX-License-Identifier: Apache-2.0

#include "private/runtime.h"
#include <iostream>
#include <unistd.h>
int main(int argc, char **argv) {
  try {
    using namespace autoconf;
    for (const auto &branch :
         {std::string("supported"), std::string("unsupported")}) {
      auto tree = std::filesystem::path(
          Runfile(argv[0], "selection/" + branch + "_app.build"));
      auto present = branch == "supported" ? "enabled" : "disabled";
      auto absent = branch == "supported" ? "disabled" : "enabled";
      auto archive = Read(tree / "libselected.a");
      Require(archive.find(std::string(present) + "_symbol") !=
                  std::string::npos,
              "missing selected symbol");
      Require(archive.find(std::string(absent) + "_symbol") ==
                  std::string::npos,
              "unselected implementation is archived");
      Require(std::filesystem::exists(tree / (std::string(present) + ".o")),
              "missing selected object");
      Require(!std::filesystem::exists(tree / (std::string(absent) + ".o")),
              "unexpected object");
      auto scratch = std::filesystem::temp_directory_path() /
                     ("selection-" + branch + "-" + std::to_string(getpid()));
      Scratch work(scratch);
      Require(Invoke({(tree / "app").string()}, {}, work.path).code == 0,
              "selected application failed");
    }
    return 0;
  } catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}
