// SPDX-License-Identifier: Apache-2.0

#include "private/runtime.h"
#include <iostream>
#include <unistd.h>
using namespace autoconf;
namespace fs = std::filesystem;
int main(int argc, char **argv) {
  try {
    Require(argc == 8, "configuration test requires seven declared artifacts");
    Require(std::getenv("TEST_SRCDIR") && std::getenv("TEST_WORKSPACE") &&
                std::getenv("TEST_UNDECLARED_OUTPUTS_DIR"),
            "must run through bazel test");
    auto root =
        fs::path(std::getenv("TEST_SRCDIR")) / std::getenv("TEST_WORKSPACE");
    auto reports = fs::path(std::getenv("TEST_UNDECLARED_OUTPUTS_DIR"));
    for (int i : {1, 3}) {
      int expected = i == 1 ? 8 : 16;
      auto answers = Parse(Read(root / argv[i] / "results.json"));
      Require(
          answers.at("CONTROL").integer() == expected &&
              answers.at("HAVE_CAPABILITY").boolean() == (expected == 8),
          "declared runfile changes did not propagate to dependent answers");
      std::string branch = expected == 8 ? "enabled" : "disabled",
                  absent = expected == 8 ? "disabled" : "enabled";
      auto tree = root / argv[i + 1];
      auto archive = Read(tree / "libselected.a");
      Require(archive.find(branch + "_symbol") != std::string::npos &&
                  archive.find(absent + "_symbol") == std::string::npos,
              "configured input did not change archive membership");
      Scratch work(fs::path(std::getenv("TEST_TMPDIR")) /
                   ("cache-check-" + std::to_string(i)));
      Require(Invoke({(tree / "app").string()}, {}, work.path).code == 0,
              "selected application failed");
      Write(
          reports / ("cache-" + branch + ".json"),
          Dump(Json::Object{{"answers", answers}, {"object", branch + ".o"}}) +
              '\n');
    }
    auto baseline = Parse(Read(root / argv[5])).at("toolchain"),
         featured = Parse(Read(root / argv[6])).at("toolchain");
    Require(Dump(baseline) != Dump(featured),
            "requested features did not change the frozen recipe");
    auto diagnostics = Parse(Read(root / argv[7] / "diagnostics.json"));
    bool found = false;
    for (const auto &record : diagnostics.array())
      for (const auto &phase : record.at("argv").array())
        for (const auto &arg : phase.array())
          found |= arg.string() == "-flto=thin";
    Require(found, "thin_lto absent from actual probe argv");
    Write(reports / "feature-tests.json",
          Dump(Json::Object{{"baseline", baseline}, {"featured", featured}}) +
              '\n');
    return 0;
  } catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}
