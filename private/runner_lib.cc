// SPDX-License-Identifier: Apache-2.0

#include "private/runtime.h"
#include <regex>
#include <set>
namespace autoconf {
namespace fs = std::filesystem;
namespace {
void Append(std::vector<std::string> &out, const std::vector<std::string> &v) {
  out.insert(out.end(), v.begin(), v.end());
}
Json Array(const std::vector<std::string> &v) {
  Json::Array a;
  for (const auto &s : v)
    a.emplace_back(s);
  return a;
}
void Check(const std::string &name, const Json &test, const Json &tools) {
  Require(std::regex_match(name, std::regex("[A-Za-z_][A-Za-z0-9_]*")),
          "invalid result name: " + name);
  auto kind = test.at("kind").string();
  std::set<std::string> keys{"kind", "failure_codes"};
  if (kind == "command") {
    for (auto key : {"tool", "arguments", "result", "timeout"})
      keys.insert(key);
    Require(tools.has(test.at("tool").string()),
            name + ": undeclared tool alias");
    auto type = test.at("result").string();
    Require(type == "bool" || type == "int" || type == "string",
            "unsupported result type");
    Require(test.at("timeout").integer() > 0 &&
                test.at("timeout").integer() <= 2147483647,
            "invalid timeout");
    Strings(test.at("arguments"));
  } else {
    Require(kind == "compile" || kind == "assemble" || kind == "link",
            "unsupported probe kind");
    keys.insert("source");
    keys.insert("flags");
    test.at("source").string();
    Strings(test.at("flags"));
  }
  Require(test.object().size() == keys.size(), "invalid descriptor schema");
  for (const auto &[key, value] : test.object())
    Require(keys.count(key), "invalid descriptor field: " + key);
  for (const auto &code : test.at("failure_codes").array())
    Require(code.integer() > 0 && code.integer() <= 255,
            "failure_codes must be positive exit statuses (1..255)");
}
} // namespace
void Run(const Json &spec, const fs::path &output) {
  fs::create_directories(output);
  const auto &tc = spec.at("toolchain");
  const auto &tools = spec.at("tools");
  Json::Object answers;
  if (spec.has("previous") && !spec.at("previous").null()) {
    answers =
        Parse(Read(fs::path(spec.at("previous").string()) / "results.json"))
            .object();
    Validate(answers);
  }
  std::vector<std::string> order;
  if (spec.has("test_order"))
    order = Strings(spec.at("test_order"));
  else
    for (const auto &[name, test] : spec.at("tests").object())
      order.push_back(name);
  std::set<std::string> names(order.begin(), order.end());
  Require(names.size() == order.size() &&
              order.size() == spec.at("tests").object().size(),
          "test_order must contain every test exactly once");
  for (const auto &name : order) {
    Check(name, spec.at("tests").at(name), tools);
    Require(!answers.count(name),
            name + ": inherited result shadowing is forbidden");
  }
  Json::Array diagnostics;
  for (const auto &name : order) {
    const auto &test = spec.at("tests").at(name);
    auto kind = test.at("kind").string();
    Scratch work(output / ("_scratch_" + name));
    auto expand = [&](const std::string &s) {
      return Substitute(s, tc, work.path.string(), answers);
    };
    auto env = Env(tc.at("compile_env"));
    if (kind == "link")
      for (const auto &[k, v] : Env(tc.at("link_env")))
        env[k] = v;
    if (spec.has("environment"))
      for (const auto &[k, v] : Env(spec.at("environment")))
        env[k] = v;
    std::vector<std::vector<std::string>> commands;
    if (kind == "command") {
      std::vector<std::string> argv{
          tools.at(test.at("tool").string()).string()};
      for (const auto &arg : Strings(test.at("arguments"))) {
        if (arg == "{compile_flags}" || arg == "{link_flags}")
          Append(argv, Strings(tc.at(arg.substr(1, arg.size() - 2))));
        else
          argv.push_back(expand(arg));
      }
      commands.push_back(argv);
    } else {
      auto source = work.path / (kind == "assemble" ? "source.S" : "source.c"),
           obj = work.path / "output.o";
      Write(source, expand(test.at("source").string()));
      std::vector<std::string> flags;
      for (const auto &f : Strings(test.at("flags")))
        flags.push_back(expand(f));
      std::vector<std::string> argv{tc.at("cc").string()};
      Append(argv, Strings(tc.at("compile_flags")));
      Append(argv, flags);
      Append(argv, {"-c", source.string(), "-o", obj.string()});
      commands.push_back(argv);
      if (kind == "link") {
        argv = {tc.at("ld").string()};
        Append(argv, Strings(tc.at("link_flags")));
        Append(argv, flags);
        Append(argv, {obj.string(), "-o", (work.path / "program").string()});
        commands.push_back(argv);
      }
    }
    Process result{};
    std::string out, err;
    Json::Array executed;
    for (const auto &argv : commands) {
      result = Invoke(argv, env, work.path,
                      kind == "command" ? test.at("timeout").integer() : 30);
      executed.push_back(Array(argv));
      out += result.out;
      err += result.err;
      if (result.code)
        break;
    }
    Write(output / (name + ".stdout"), out);
    Write(output / (name + ".stderr"), err);
    auto type = kind == "command" ? test.at("result").string() : "bool";
    bool rejection = false;
    for (const auto &code : test.at("failure_codes").array())
      rejection |= code.integer() == result.code;
    Require(result.code == 0 || (type == "bool" && rejection),
            name + ": unexpected exit " + std::to_string(result.code) + ": " +
                Dump(executed) + '\n' + err);
    Json value;
    if (type == "bool")
      value = Json(result.code == 0);
    else if (type == "int") {
      auto s = Trim(out);
      Require(std::regex_match(s, std::regex("[+-]?[0-9]+")),
              name + ": invalid decimal integer: " + s);
      bool negative = s[0] == '-';
      size_t start =
          s.find_first_not_of('0', (s[0] == '-' || s[0] == '+') ? 1 : 0);
      value =
          Json(Json::Integer{start == std::string::npos
                                 ? "0"
                                 : (negative ? "-" : "") + s.substr(start)});
    } else
      value = Json(Trim(out));
    Validate({{name, value}});
    answers[name] = value;
    diagnostics.emplace_back(Json::Object{{"name", name},
                                          {"argv", executed},
                                          {"returncode", result.code},
                                          {"value", value}});
  }
  Publish(output, answers);
  Write(output / "diagnostics.json", Dump(diagnostics) + '\n');
}
} // namespace autoconf
