// SPDX-License-Identifier: Apache-2.0

#include "private/runtime.h"
#include <iostream>
extern char **environ;
int main(int argc, char **argv) {
  try {
    using namespace autoconf;
    Require(argc >= 4, "usage: sizeof WORK TYPE CC FLAGS...");
    std::filesystem::path work = argv[1], source = work / "sizeof.c",
                          obj = work / "sizeof.o";
    Environment env;
    for (char **p = environ; p && *p; ++p) {
      std::string s = *p;
      auto at = s.find('=');
      env[s.substr(0, at)] = s.substr(at + 1);
    }
    for (int size : {1, 2, 4, 8, 16}) {
      Write(source, "#include <stdlib.h>\n#include <stddef.h>\n#include "
                    "<stdint.h>\n#include <inttypes.h>\n#include "
                    "<sys/types.h>\n_Static_assert(sizeof(" +
                        std::string(argv[2]) + ") == " + std::to_string(size) +
                        ", \"size\");");
      std::vector<std::string> args{argv[3]};
      for (int i = 4; i < argc; ++i)
        args.push_back(argv[i]);
      for (const std::string &s : {std::string("-c"), source.string(),
                                   std::string("-o"), obj.string()})
        args.push_back(s);
      auto result = Invoke(args, env, work);
      Require(result.code == 0 || result.code == 1,
              "compiler infrastructure failure");
      if (result.code == 0) {
        std::cout << size << '\n';
        return 0;
      }
    }
    throw std::runtime_error("unsupported or missing target type");
  } catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    return 2;
  }
}
