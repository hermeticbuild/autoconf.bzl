// SPDX-License-Identifier: Apache-2.0

#include "private/runtime.h"
#include <iostream>
int main(int argc, char **argv) {
  try {
    using namespace autoconf;
    Require(argc >= 3, "planner arguments missing");
    if (std::string(argv[1]) == "initial") {
      PublishPayload(argv[2], Json::Object{{"answers", Json::Object{}},
                                           {"history", Json::Array{}},
                                           {"cache", Json::Object{}}});
      return 0;
    }
    Require(argc == 4, "planner requires manifest, previous and output trees");
    auto request = Parse(Read(std::filesystem::path(argv[1]) / "request.json"));
    auto state =
        Parse(Read(std::filesystem::path(argv[2]) / "payload.json")).object();
    const auto &query = request.at("query");
    if (!query.null()) {
      const auto &tc = request.at("toolchain");
      auto identity = Dump(Json::Object{
          {"query", query}, {"toolchain", tc}, {"prior", query.at("prior")}});
      auto cache = state.at("cache").object();
      if (!cache.count(identity)) {
        std::filesystem::create_directories(argv[3]);
        Scratch work(std::filesystem::path(argv[3]) / "scratch");
        auto source = work.path / "query.c", obj = work.path / "query.o";
        Write(source, query.at("source").string());
        std::vector<std::string> args{tc.at("cc").string()};
        for (const auto &flag : Strings(tc.at("compile_flags")))
          args.push_back(flag);
        for (const auto &v : {std::string("-c"), source.string(),
                              std::string("-o"), obj.string()})
          args.push_back(v);
        auto result = Invoke(args, Env(tc.at("compile_env")), work.path);
        Require(result.code == 0 || result.code == 1,
                "query infrastructure failure");
        cache[identity] = Json(result.code == 0);
      }
      auto answers = state.at("answers").object();
      answers[query.at("name").string()] = cache.at(identity);
      auto history = state.at("history").array();
      history.emplace_back(
          Json::Object{{"identity", identity}, {"value", cache.at(identity)}});
      state["cache"] = cache;
      state["answers"] = answers;
      state["history"] = history;
    }
    PublishPayload(argv[3], state);
    return 0;
  } catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}
