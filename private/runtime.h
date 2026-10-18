// SPDX-License-Identifier: Apache-2.0

#ifndef AUTOCONF_RUNTIME_H
#define AUTOCONF_RUNTIME_H

#include <cstdint>
#include <cstdlib>
#if defined(_WIN32)
#error autoconf execution helpers currently require a POSIX execution platform
#endif
#include <filesystem>
#include <map>
#include <string>
#include <variant>
#include <vector>

namespace autoconf {
// Configuration JSON uses null, bool, decimal integers, strings, arrays and
// objects. Preserve scalar integers as canonical text without a machine-size
// bound; bounded control fields explicitly convert through integer().
struct Json {
  struct Integer {
    std::string text;
  };
  using Array = std::vector<Json>;
  using Object = std::map<std::string, Json>;
  std::variant<std::nullptr_t, bool, Integer, std::string, Array, Object> value;
  Json() : value(nullptr) {}
  Json(bool v) : value(v) {}
  Json(int v) : value(Integer{std::to_string(v)}) {}
  Json(int64_t v) : value(Integer{std::to_string(v)}) {}
  Json(Integer v) : value(std::move(v)) {}
  Json(const char *v) : value(std::string(v)) {}
  Json(std::string v) : value(std::move(v)) {}
  Json(Array v) : value(std::move(v)) {}
  Json(Object v) : value(std::move(v)) {}
  const Json &at(const std::string &key) const;
  bool has(const std::string &key) const;
  const std::string &string() const;
  const Array &array() const;
  const Object &object() const;
  int64_t integer() const;
  bool boolean() const;
  bool null() const;
};
Json Parse(const std::string &text);
std::string Dump(const Json &value);
std::string Read(const std::filesystem::path &path);
void Write(const std::filesystem::path &path, const std::string &text);
using Environment = std::map<std::string, std::string>;
Environment Env(const Json &json);
std::vector<std::string> Strings(const Json &json);
std::string Trim(const std::string &text);
void Require(bool condition, const std::string &message);
struct Process {
  int code;
  std::string out, err;
};
Process Invoke(std::vector<std::string> argv, const Environment &env,
               const std::filesystem::path &scratch, int timeout = 30);
struct Scratch {
  std::filesystem::path path;
  explicit Scratch(std::filesystem::path p);
  ~Scratch();
  Scratch(const Scratch &) = delete;
};
void Validate(const Json::Object &values);
Json::Object Markers(const std::filesystem::path &directory);
void Publish(const std::filesystem::path &directory,
             const Json::Object &values);
void PublishPayload(const std::filesystem::path &directory, const Json &value);
std::string Substitute(const std::string &text, const Json &toolchain,
                       const std::string &work, const Json::Object &answers);
void Run(const Json &spec, const std::filesystem::path &output);
std::string Runfile(const std::string &argv0, const std::string &suffix);
} // namespace autoconf
#endif
