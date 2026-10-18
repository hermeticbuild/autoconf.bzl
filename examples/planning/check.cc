// SPDX-License-Identifier: Apache-2.0

#include "private/runtime.h"
#include <iostream>
int main(int argc, char **argv) {
  try {
    using namespace autoconf;
    auto state =
        Parse(Read(Runfile(argv[0], "planning/converged.state3/payload.json")));
    Require(state.at("history").array().size() == 2 &&
                state.at("cache").object().size() == 2 &&
                state.at("answers").object().size() == 2,
            "planning did not converge after two unique measurements");
    Require(std::filesystem::file_size(
                Runfile(argv[0], "planning/converged.build/configured.o")) > 0,
            "missing compiled object");
    auto incomplete =
        Parse(Read(Runfile(argv[0], "planning/too_short.state1/payload.json")));
    Require(incomplete.at("answers").object().size() == 1 &&
                !incomplete.at("answers").has("INT128") &&
                !incomplete.at("answers").has("INT64"),
            "one stage must leave the dependent query unmeasured");
    return 0;
  } catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}
