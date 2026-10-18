// SPDX-License-Identifier: Apache-2.0

#include "private/runtime.h"
#include <cstdlib>
#include <functional>
#include <iostream>
#include <unistd.h>
using namespace autoconf;
namespace fs = std::filesystem;
void Error(const std::function<void()> &action,
           const std::string &expected = "") {
  try {
    action();
  } catch (const std::exception &e) {
    Require(std::string(e.what()).find(expected) != std::string::npos,
            "wrong failure: " + std::string(e.what()));
    return;
  }
  throw std::runtime_error("expected failure: " + expected);
}
Json Command(Json::Array args, const std::string &type = "bool",
             int timeout = 30) {
  return Json::Object{{"kind", "command"},
                      {"tool", "test"},
                      {"arguments", args},
                      {"result", type},
                      {"failure_codes", Json::Array{1}},
                      {"timeout", timeout}};
}
int main(int argc, char **argv) {
  try {
    auto root = fs::temp_directory_path() /
                ("autoconf-test-" + std::to_string(getpid()));
    Scratch cleanup(root);
    auto tool = Runfile(argv[0], "tool");
    Json tc = Json::Object{
        {"cc", "/compiler"},           {"ld", "/linker"},
        {"ar", "/archiver"},           {"compile_flags", Json::Array{"-DTEST"}},
        {"link_flags", Json::Array{}}, {"compile_env", Json::Object{}},
        {"link_env", Json::Object{}}};
    int index = 0;
    auto execute = [&](const Json::Object &tests,
                       const std::string &previous = "",
                       const std::string &executable = "") {
      auto output = root / std::to_string(index++);
      Json spec = Json::Object{
          {"tests", tests},
          {"toolchain", tc},
          {"tools",
           Json::Object{{"test", executable.empty() ? tool : executable}}},
          {"environment", Json::Object{}},
          {"previous", previous.empty() ? Json() : Json(previous)}};
      try {
        Run(spec, output);
      } catch (...) {
        for (const auto &file : fs::directory_iterator(output))
          Require(file.path().filename().string().rfind("_scratch_", 0) != 0,
                  "scratch leaked after failure");
        throw;
      }
      auto answers = Parse(Read(output / "results.json"));
      Require(Dump(answers) == Dump(Markers(output)),
              "JSON and markers disagree");
      for (const auto &file : fs::directory_iterator(output))
        Require(file.path().filename().string().rfind("_scratch_", 0) != 0,
                "scratch leaked on success");
      return answers;
    };
    auto values =
        execute({{"YES", Command({"exit", "0"})},
                 {"NO", Command({"exit", "1"})},
                 {"NUMBER", Command({"scalar", " +008 "}, "int")},
                 {"FAMILY", Command({"scalar", "llvm-20.1"}, "string")}});
    Require(values.at("YES").boolean() && !values.at("NO").boolean() &&
                values.at("NUMBER").integer() == 8 &&
                values.at("FAMILY").string() == "llvm-20.1",
            "incorrect typed results");
    for (const auto &args : {Json::Array{"exit", "127"}, Json::Array{"signal"},
                             Json::Array{"timeout"}})
      Error([&] { execute({{"FAIL", Command(args, "bool", 1)}}); });
    for (const auto &text : {"", "a/b", "two words", "..", ".", "é"})
      Error([&] { execute({{"FAIL", Command({"scalar", text}, "string")}}); },
            "unsafe string");
    for (const auto &text : {"no", "12.0", "12x"})
      Error([&] { execute({{"FAIL", Command({"scalar", text}, "int")}}); });
    Error([&] { execute({{"FAIL", Command({"exit", "1"}, "int")}}); },
          "unexpected exit");
    Error([&] { execute({{"FAIL", Command({"scalar", "{result:MISSING}"})}}); },
          "unresolved substitution");
    Error(
        [&] {
          execute({{"FAIL", Command({"exit", "0"})}}, "",
                  "/missing-autoconf-executable");
        },
        "infrastructure");
    setenv("AUTOCONF_UNDECLARED", "do-not-inherit", 1);
    execute({{"CLEAN", Command({"environment"})}});
    auto large = execute(
        {{"LARGE",
          Command({"scalar", "+000123456789012345678901234567890"}, "int")}});
    Require(Dump(large.at("LARGE")) == "123456789012345678901234567890",
            "large integers must retain exact decimal values");
    auto previous = root / "previous";
    Publish(previous, {{"ABI", 8}});
    Require(execute({{"NEXT", Command({"scalar", "{result:ABI}"}, "int")}},
                    previous.string())
                    .at("NEXT")
                    .integer() == 8,
            "previous substitution failed");
    Error(
        [&] { execute({{"ABI", Command({"exit", "0"})}}, previous.string()); },
        "shadowing");
    for (auto name : {"../escape", "9bad", "a=b"})
      Error([&] { execute({{name, Command({"exit", "0"})}}); },
            "invalid result name");
    auto invalid = Command({"exit", "0"}).object();
    invalid["failure_codes"] = Json::Array{0};
    Error([&] { execute({{"BAD", invalid}}); }, "positive exit statuses");
    Require(Substitute("int a[] = {array}; int f() { return {result:B}; }", tc,
                       "work", {{"B", false}}) ==
                "int a[] = {array}; int f() { return false; }",
            "ordinary C braces corrupted");
    Require(Substitute("{result:X}", tc, "work", {{"X", "{ld}"}}) == "{ld}",
            "substitutions must be single-pass");
    Error([&] { Substitute("{compile_flags}", tc, "work", {}); }, "reserved");
    for (const auto &name : {"X=bool=true", "X=int=01", "X=int=-0",
                             "X=unknown=1", "X=string=..", "9X=bool=1"}) {
      auto dir = root / ("wire" + std::to_string(index++));
      Write(dir / name, "");
      Error([&] { Markers(dir); });
    }
    auto duplicate = root / "duplicate";
    Write(duplicate / "X=bool=1", "");
    Write(duplicate / "X=int=2", "");
    Error([&] { Markers(duplicate); }, "duplicate");
    Require(Dump(Parse("{\"unicode\":\"\\u2603\\ud83d\\ude00\"}")) ==
                "{\"unicode\":\"\\u2603\\ud83d\\ude00\"}",
            "Unicode JSON roundtrip failed");
    for (auto text : {"{\"x\":1,\"x\":2}", "[1,]", "1.5", "01", "\"\\ud800\""})
      Error([&] { Parse(text); });
    PublishPayload(root / "payload",
                   Parse("{\"nested\":[\"\\u2603\",true,42]}"));
    Error(
        [&] { PublishPayload(root / "huge", Json(std::string(1048576, 'x'))); },
        "1 MiB");
    std::cout << "runner, process failure, schema, wire, payload and cleanup "
                 "tests passed\n";
    return 0;
  } catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}
