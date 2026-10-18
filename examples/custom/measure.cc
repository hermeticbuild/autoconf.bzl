// SPDX-License-Identifier: Apache-2.0

#include "private/runtime.h"
#include <iostream>
extern char **environ;
int main(int argc, char **argv) {
  try {
    using namespace autoconf;
    Require(argc >= 4, "usage: measure MODE WORK CC FLAGS...");
    auto resource = Runfile(argv[0], "custom/family.txt");
    Require(Trim(Read(resource)) == "compiler",
            "declared runfile has unexpected contents");
    std::string mode = argv[1];
    if (mode == "bool")
      return 0;
    std::filesystem::path work = argv[2], source = work / "measure.c",
                          obj = work / "measure.o";
    auto compiles = [&](const std::string &text) {
      Write(source, text);
      std::vector<std::string> args{argv[3]};
      for (int i = 4; i < argc; ++i)
        args.push_back(argv[i]);
      for (const auto &s : {std::string("-Werror"), std::string("-c"),
                            source.string(), std::string("-o"), obj.string()})
        args.push_back(s);
      // The tool's environment is already the runner's explicit contract.
      extern char **environ;
      Environment env;
      for (char **p = ::environ; p && *p; ++p) {
        std::string s = *p;
        auto at = s.find('=');
        env[s.substr(0, at)] = s.substr(at + 1);
      }
      auto result = Invoke(args, env, work);
      Require(result.code == 0 || result.code == 1,
              "compiler infrastructure failure");
      return result.code == 0;
    };
    if (mode == "sizeof-long") {
      for (int size : {1, 2, 4, 8, 16})
        if (compiles("_Static_assert(sizeof(long) == " + std::to_string(size) +
                     ", \"size\");")) {
          std::cout << size << '\n';
          return 0;
        }
    } else if (mode == "family") {
      for (const auto &pair :
           {std::pair<std::string, std::string>{"__clang__", "clang"},
            {"__GNUC__", "gcc"}})
        if (compiles("#ifndef " + pair.first +
                     "\n#error wrong compiler\n#endif\n")) {
          std::cout << pair.second << '\n';
          return 0;
        }
    }
    throw std::runtime_error("unsupported ABI or compiler family");
  } catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    return 2;
  }
}
