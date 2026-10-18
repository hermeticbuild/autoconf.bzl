// SPDX-License-Identifier: Apache-2.0

// Developer reference workflow; never used to configure a Bazel build action.
#include "private/runtime.h"
#include <cctype>
#include <iostream>
#include <regex>
#include <sstream>
#include <unistd.h>
using namespace autoconf;
namespace fs = std::filesystem;
Json Rebase(Json value, const Json::Object &roots) {
  if (std::holds_alternative<std::string>(value.value)) {
    auto text = value.string();
    size_t position = std::string::npos;
    std::string before, after;
    for (const auto &[source, destination] : roots) {
      auto at = text.find(source + '/');
      if (at < position || (at == position && source.size() > before.size())) {
        position = at;
        before = source;
        after = destination.string();
      }
    }
    if (position != std::string::npos)
      text.replace(position, before.size(), after);
    return text;
  }
  if (std::holds_alternative<Json::Array>(value.value)) {
    Json::Array result;
    for (const auto &item : value.array())
      result.push_back(Rebase(item, roots));
    return result;
  }
  if (std::holds_alternative<Json::Object>(value.value)) {
    Json::Object result;
    for (const auto &[key, item] : value.object())
      result[key] = Rebase(item, roots);
    return result;
  }
  return value;
}
int main(int argc, char **argv) {
  try {
    if (const char *path = std::getenv("AUTOCONF_NATIVE_TOOLCHAIN")) {
      auto tc = Parse(Read(path));
      std::vector<std::string> args{tc.at("cc").string()};
      for (const auto &flag : Strings(tc.at("compile_flags")))
        args.push_back(flag);
      bool link = true;
      for (int i = 1; i < argc; ++i) {
        std::string s = argv[i];
        args.push_back(s);
        if (s == "-c" || s == "-E" || s == "-S" || s == "-v" ||
            s == "--version" || s == "-V" || s == "-qversion")
          link = false;
      }
      auto env = Env(tc.at("compile_env"));
      if (link) {
        for (const auto &f : Strings(tc.at("link_flags")))
          args.push_back(f);
        for (const auto &[k, v] : Env(tc.at("link_env")))
          env[k] = v;
      }
      Scratch work(fs::temp_directory_path() /
                   ("ogg-compiler-" + std::to_string(getpid())));
      auto result = Invoke(args, env, work.path, 60);
      std::cout << result.out;
      std::cerr << result.err;
      return result.code < 0 ? 128 - result.code : result.code;
    }
    Require(argc == 1 && std::getenv("TEST_UNDECLARED_OUTPUTS_DIR") &&
                std::getenv("TEST_SRCDIR") && std::getenv("TEST_WORKSPACE"),
            "must run through bazel test //libogg:native_reference_test");
#ifdef __APPLE__
    std::string flavor = "macos";
#elif defined(__linux__)
    std::string flavor = "linux";
#else
    throw std::runtime_error(
        "native reference supports hosted macOS/Linux only");
#endif
    auto spec = Parse(
        Read(Runfile(argv[0], "libogg/features_" + flavor + ".spec.json")));
    auto expected = Parse(Read(Runfile(argv[0], "libogg/features_" + flavor +
                                                    ".results/results.json")));
    auto configure = Runfile(argv[0], "configure");
    fs::path output = fs::absolute(std::getenv("TEST_UNDECLARED_OUTPUTS_DIR")),
             self = fs::canonical(argv[0]);
    fs::create_directories(output);
    // Require an unused directory: never recursively delete caller-owned data.
    Require(
        !fs::exists(output / "config.status"),
        "native output already configured; choose a fresh output directory");
    auto mapping =
        Parse(Read(Runfile(argv[0], "libogg/native_reference_test.paths.json")))
            .object();
    auto runfiles_main =
        fs::path(std::getenv("TEST_SRCDIR")) / std::getenv("TEST_WORKSPACE");
    for (auto &[source, destination] : mapping)
      destination =
          (runfiles_main / destination.string()).lexically_normal().string();
    auto tc = Rebase(spec.at("toolchain"), mapping);
    Write(output / "toolchain.json", Dump(tc));
    Scratch work(output / "scratch");
    fs::create_symlink(self, work.path / "compiler");
    auto multicall = fs::canonical(tc.at("cc").string());
    auto aliases = work.path / "tools";
    fs::create_directories(aliases);
    // The declared compiler's LLVM multicall binary also implements binutils.
    // Materialize only dispatch names here; executable contents stay declared.
    Environment env{{"PATH", aliases.string() + ":/usr/bin:/bin"},
                    {"LANG", "C"},
                    {"LC_ALL", "C"},
                    {"HOME", work.path.string()},
                    {"TMPDIR", work.path.string()}};
    std::map<std::string, std::string> utilities{
        {"ar", "llvm-ar"},
        {"ld", flavor == "linux" ? "ld.lld" : "ld64.lld"},
        {"nm", "llvm-nm"},
        {"ranlib", "llvm-ranlib"},
        {"strip", "llvm-strip"},
        {"objdump", "llvm-objdump"},
        {"objcopy", "llvm-objcopy"}};
    for (const auto &[name, executable] : utilities) {
      auto tool = aliases / executable;
      fs::create_symlink(multicall, tool);
      std::string variable = name;
      for (auto &c : variable)
        c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
      env[variable] = tool.string();
    }
    env["AUTOCONF_NATIVE_TOOLCHAIN"] = (output / "toolchain.json").string();
    env["CC"] = (work.path / "compiler").string();
    for (const auto &key : {"CFLAGS", "CPPFLAGS", "LDFLAGS", "LIBS"})
      env[key] = "";
    env["CONFIG_SITE"] = "/dev/null";
    fs::current_path(output);
    auto target = Invoke({env["CC"], "-dumpmachine"}, env, work.path, 30);
    Require(target.code == 0,
            "cannot determine native-reference target: " + target.err);
    auto triple = Trim(target.out);
    Require(std::regex_match(triple, std::regex("[A-Za-z0-9_.+-]+")),
            "invalid compiler target triple: " + triple);
    auto configured = Invoke(
        {configure, "--build=" + triple, "--host=" + triple, "--disable-shared",
         "--enable-static", "--enable-crc", "--disable-dependency-tracking"},
        env, work.path, 180);
    Write(output / "configure.log", configured.out + configured.err);
    Require(configured.code == 0,
            "native configure failed:\n" + configured.out + configured.err);
    std::map<std::string, std::string> defines;
    std::istringstream lines(Read(output / "config.h"));
    std::string line;
    std::regex define("#define ([A-Za-z_][A-Za-z_0-9]*) (.*)");
    std::smatch match;
    while (std::getline(lines, line))
      if (std::regex_match(line, match, define))
        defines[match[1]] = match[2];
    Json::Object normalized;
    Json::Array differences;
    for (const auto &[key, value] : expected.object()) {
      int64_t actual = defines.count(key) ? std::stoll(defines[key]) : 0;
      Json measured = std::holds_alternative<bool>(value.value)
                          ? Json(actual != 0)
                          : Json(actual);
      normalized[key] = measured;
      if (Dump(value) != Dump(measured))
        differences.emplace_back(Json::Object{
            {"name", key}, {"bazel", value}, {"native", measured}});
    }
    if (defines.count("DISABLE_CRC"))
      differences.emplace_back(
          Json::Object{{"name", "DISABLE_CRC"},
                       {"bazel", "undefined"},
                       {"native", defines["DISABLE_CRC"]}});
    auto types = Read(output / "include/ogg/config_types.h");
    for (int width : {16, 32, 64})
      for (const auto &prefix : {"int", "uint"}) {
        auto token = std::string(prefix) + std::to_string(width) + "_t";
        if (types.find("typedef " + token + " ogg_" + token + ';') ==
            std::string::npos)
          differences.emplace_back(
              Json::Object{{"name", "ogg_" + token},
                           {"bazel", token},
                           {"native", "different typedef"}});
      }
    Write(output / "comparison.json",
          Dump(Json::Object{{"project", "libogg"},
                            {"version", "1.3.5"},
                            {"bazel", expected},
                            {"native", normalized},
                            {"differences", differences}}) +
              '\n');
    Require(differences.empty(),
            "configuration mismatch: " + Dump(differences));
    std::cout << "Native configure matches all " << expected.object().size()
              << " values and typedefs.\n";
    return 0;
  } catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}
