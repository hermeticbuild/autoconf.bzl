// SPDX-License-Identifier: Apache-2.0

#include "private/runtime.h"

#include <cerrno>
#include <chrono>
#include <csignal>
#include <cstring>
#include <fcntl.h>
#include <fstream>
#include <iomanip>
#include <limits>
#include <regex>
#include <set>
#include <sstream>
#include <stdexcept>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>

namespace autoconf {
namespace fs = std::filesystem;
void Require(bool condition, const std::string &message) {
  if (!condition)
    throw std::runtime_error(message);
}
const Json &Json::at(const std::string &key) const {
  auto it = object().find(key);
  Require(it != object().end(), "missing JSON field: " + key);
  return it->second;
}
bool Json::has(const std::string &key) const { return object().count(key); }
const std::string &Json::string() const { return std::get<std::string>(value); }
const Json::Array &Json::array() const { return std::get<Array>(value); }
const Json::Object &Json::object() const { return std::get<Object>(value); }
int64_t Json::integer() const {
  return std::stoll(std::get<Integer>(value).text);
}
bool Json::boolean() const { return std::get<bool>(value); }
bool Json::null() const {
  return std::holds_alternative<std::nullptr_t>(value);
}

namespace {
void Utf8(std::string &out, unsigned code) {
  Require(code <= 0x10ffff && !(code >= 0xd800 && code <= 0xdfff),
          "invalid Unicode scalar");
  if (code < 128)
    out += char(code);
  else if (code < 2048) {
    out += char(0xc0 | code >> 6);
    out += char(0x80 | (code & 63));
  } else if (code < 65536) {
    out += char(0xe0 | code >> 12);
    out += char(0x80 | ((code >> 6) & 63));
    out += char(0x80 | (code & 63));
  } else {
    out += char(0xf0 | code >> 18);
    out += char(0x80 | ((code >> 12) & 63));
    out += char(0x80 | ((code >> 6) & 63));
    out += char(0x80 | (code & 63));
  }
}
class Parser {
  const std::string &text;
  size_t pos = 0;
  char peek() const { return pos < text.size() ? text[pos] : '\0'; }
  char take() {
    Require(pos < text.size(), "truncated JSON");
    return text[pos++];
  }
  void space() {
    while (peek() && std::string(" \t\r\n").find(peek()) != std::string::npos)
      ++pos;
  }
  void expect(char c) { Require(take() == c, "invalid JSON punctuation"); }
  unsigned hex() {
    unsigned value = 0;
    for (int i = 0; i < 4; ++i) {
      char c = take();
      auto at = std::string("0123456789abcdef")
                    .find(char(std::tolower(static_cast<unsigned char>(c))));
      Require(at != std::string::npos, "invalid JSON Unicode escape");
      value = value * 16 + at;
    }
    return value;
  }
  std::string string() {
    expect('"');
    std::string result;
    while (peek() != '"') {
      unsigned char c = take();
      Require(c >= 32, "control byte in JSON string");
      if (c != '\\') {
        result += char(c);
        continue;
      }
      c = take();
      switch (c) {
      case '"':
      case '\\':
      case '/':
        result += char(c);
        break;
      case 'b':
        result += '\b';
        break;
      case 'f':
        result += '\f';
        break;
      case 'n':
        result += '\n';
        break;
      case 'r':
        result += '\r';
        break;
      case 't':
        result += '\t';
        break;
      case 'u': {
        unsigned code = hex();
        if (code >= 0xd800 && code <= 0xdbff) {
          expect('\\');
          expect('u');
          unsigned low = hex();
          Require(low >= 0xdc00 && low <= 0xdfff,
                  "invalid JSON surrogate pair");
          code = 0x10000 + ((code - 0xd800) << 10) + low - 0xdc00;
        }
        Utf8(result, code);
        break;
      }
      default:
        throw std::runtime_error("invalid JSON escape");
      }
    }
    expect('"');
    return result;
  }

public:
  explicit Parser(const std::string &t) : text(t) {}
  Json value(int depth = 0) {
    Require(depth <= 128, "JSON nesting limit exceeded");
    space();
    char c = peek();
    if (c == '"')
      return string();
    if (c == '[') {
      take();
      space();
      Json::Array items;
      if (peek() != ']')
        for (;;) {
          items.push_back(value(depth + 1));
          space();
          if (peek() != ',')
            break;
          take();
        }
      expect(']');
      return items;
    }
    if (c == '{') {
      take();
      space();
      Json::Object items;
      if (peek() != '}')
        for (;;) {
          space();
          std::string key = string();
          space();
          expect(':');
          Require(items.emplace(key, value(depth + 1)).second,
                  "duplicate JSON key: " + key);
          space();
          if (peek() != ',')
            break;
          take();
        }
      expect('}');
      return items;
    }
    for (const auto &token :
         {std::string("null"), std::string("true"), std::string("false")}) {
      if (text.compare(pos, token.size(), token) == 0) {
        pos += token.size();
        if (token == "null")
          return Json();
        return Json(token == "true");
      }
    }
    size_t start = pos;
    if (peek() == '-')
      take();
    Require(peek() >= '0' && peek() <= '9', "expected JSON value");
    if (peek() == '0')
      take();
    else
      while (peek() >= '0' && peek() <= '9')
        take();
    auto number = text.substr(start, pos - start);
    return Json(Json::Integer{number == "-0" ? "0" : number});
  }
  Json document() {
    auto result = value();
    space();
    Require(pos == text.size(), "trailing JSON data or unsupported number");
    return result;
  }
};
std::string Quote(const std::string &text) {
  std::ostringstream out;
  out << '"';
  auto escape = [&](unsigned code) {
    out << "\\u" << std::hex << std::setw(4) << std::setfill('0') << code
        << std::dec;
  };
  for (size_t i = 0; i < text.size(); ++i) {
    unsigned char c = text[i];
    if (c == '"' || c == '\\')
      out << '\\' << char(c);
    else if (c < 32)
      escape(c);
    else if (c < 128)
      out << char(c);
    else {
      unsigned code, count, minimum;
      if ((c & 0xe0) == 0xc0) {
        code = c & 31;
        count = 1;
        minimum = 128;
      } else if ((c & 0xf0) == 0xe0) {
        code = c & 15;
        count = 2;
        minimum = 2048;
      } else {
        Require((c & 0xf8) == 0xf0, "invalid UTF-8");
        code = c & 7;
        count = 3;
        minimum = 65536;
      }
      for (unsigned j = 0; j < count; ++j) {
        Require(++i < text.size() &&
                    (static_cast<unsigned char>(text[i]) & 0xc0) == 0x80,
                "invalid UTF-8 continuation");
        code = (code << 6) | (text[i] & 63);
      }
      Require(code >= minimum && code <= 0x10ffff &&
                  !(code >= 0xd800 && code <= 0xdfff),
              "invalid UTF-8 scalar");
      if (code < 65536)
        escape(code);
      else {
        code -= 65536;
        escape(0xd800 | (code >> 10));
        escape(0xdc00 | (code & 1023));
      }
    }
  }
  out << '"';
  return out.str();
}
const std::regex kName("[A-Za-z_][A-Za-z0-9_]*");
const std::regex kToken("[A-Za-z0-9_.+\\-]{1,120}");
const std::regex kDecimal("0|-[1-9][0-9]*|[1-9][0-9]*");
std::string Scalar(const Json &value) {
  return std::holds_alternative<std::string>(value.value) ? value.string()
                                                          : Dump(value);
}
} // namespace
Json Parse(const std::string &text) { return Parser(text).document(); }
std::string Dump(const Json &v) {
  if (v.null())
    return "null";
  if (std::holds_alternative<bool>(v.value))
    return v.boolean() ? "true" : "false";
  if (std::holds_alternative<Json::Integer>(v.value))
    return std::get<Json::Integer>(v.value).text;
  if (std::holds_alternative<std::string>(v.value))
    return Quote(v.string());
  std::string result;
  if (std::holds_alternative<Json::Array>(v.value)) {
    result = "[";
    for (const auto &item : v.array()) {
      if (result.size() > 1)
        result += ',';
      result += Dump(item);
    }
    return result + ']';
  }
  result = "{";
  for (const auto &[key, value] : v.object()) {
    if (result.size() > 1)
      result += ',';
    result += Quote(key) + ':' + Dump(value);
  }
  return result + '}';
}
std::string Read(const fs::path &path) {
  std::ifstream in(path, std::ios::binary);
  Require(bool(in), "cannot read: " + path.string());
  return std::string(std::istreambuf_iterator<char>(in), {});
}
void Write(const fs::path &path, const std::string &text) {
  fs::create_directories(path.parent_path().empty() ? "." : path.parent_path());
  std::ofstream out(path, std::ios::binary);
  out << text;
  Require(bool(out), "cannot write: " + path.string());
}
Environment Env(const Json &json) {
  Environment env;
  for (const auto &[k, v] : json.object())
    env[k] = v.string();
  return env;
}
std::vector<std::string> Strings(const Json &json) {
  std::vector<std::string> result;
  for (const auto &v : json.array())
    result.push_back(v.string());
  return result;
}
std::string Trim(const std::string &text) {
  auto first = text.find_first_not_of(" \t\r\n\v\f");
  return first == std::string::npos
             ? ""
             : text.substr(first,
                           text.find_last_not_of(" \t\r\n\v\f") - first + 1);
}
Scratch::Scratch(fs::path p) : path(std::move(p)) {
  Require(fs::create_directory(path),
          "scratch directory already exists: " + path.string());
}
Scratch::~Scratch() {
  std::error_code ec;
  fs::remove_all(path, ec);
}
Process Invoke(std::vector<std::string> argv, const Environment &env,
               const fs::path &scratch, int timeout) {
  Require(!argv.empty() && timeout > 0, "empty command or invalid timeout");
  if (argv[0].find('/') == std::string::npos)
    argv[0] = "./" + argv[0];
  std::vector<char *> args, vars;
  std::vector<std::string> storage;
  for (const auto &[key, value] : env) {
    Require(key.find('=') == std::string::npos && !key.empty(),
            "invalid environment key");
    storage.push_back(key + "=" + value);
  }
  for (auto &arg : argv) {
    Require(arg.find('\0') == std::string::npos, "NUL in command");
    args.push_back(arg.data());
  }
  for (auto &var : storage) {
    Require(var.find('\0') == std::string::npos, "NUL in environment");
    vars.push_back(var.data());
  }
  args.push_back(nullptr);
  vars.push_back(nullptr);
  int out = open((scratch / "process.stdout").c_str(),
                 O_WRONLY | O_CREAT | O_TRUNC, 0600);
  int err = open((scratch / "process.stderr").c_str(),
                 O_WRONLY | O_CREAT | O_TRUNC, 0600);
  Require(out >= 0 && err >= 0, "cannot open subprocess logs");
  int errors[2];
  Require(pipe(errors) == 0, "cannot create exec error pipe");
  fcntl(errors[1], F_SETFD, FD_CLOEXEC);
  pid_t pid = fork();
  if (pid == 0) {
    close(errors[0]);
    setpgid(0, 0);
    dup2(out, STDOUT_FILENO);
    dup2(err, STDERR_FILENO);
    close(out);
    close(err);
    execve(argv[0].c_str(), args.data(), vars.data());
    int failure = errno;
    (void)!write(errors[1], &failure, sizeof(failure));
    _exit(127);
  }
  close(out);
  close(err);
  close(errors[1]);
  Require(pid > 0, "fork failed");
  int failure = 0;
  ssize_t count;
  do {
    count = read(errors[0], &failure, sizeof(failure));
  } while (count < 0 && errno == EINTR);
  close(errors[0]);
  int status = 0;
  auto deadline =
      std::chrono::steady_clock::now() + std::chrono::seconds(timeout);
  for (;;) {
    pid_t done = waitpid(pid, &status, WNOHANG);
    if (done == pid)
      break;
    if (done < 0) {
      if (errno == EINTR)
        continue;
      throw std::runtime_error("waitpid failed");
    }
    if (std::chrono::steady_clock::now() >= deadline) {
      kill(-pid, SIGKILL);
      kill(pid, SIGKILL);
      while (waitpid(pid, &status, 0) < 0 && errno == EINTR) {
      }
      throw std::runtime_error("probe infrastructure failure: timeout: " +
                               argv[0]);
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
  }
  Require(count == 0, "probe infrastructure failure: cannot execute " +
                          argv[0] + ": " + std::strerror(failure));
  return {WIFSIGNALED(status) ? -WTERMSIG(status) : WEXITSTATUS(status),
          Read(scratch / "process.stdout"), Read(scratch / "process.stderr")};
}
void Validate(const Json::Object &values) {
  for (const auto &[name, value] : values) {
    Require(std::regex_match(name, kName), "invalid result name: " + name);
    if (std::holds_alternative<std::string>(value.value)) {
      auto s = value.string();
      Require(std::regex_match(s, kToken) && s != "." && s != "..",
              name + ": unsafe string");
    } else
      Require(std::holds_alternative<bool>(value.value) ||
                  std::holds_alternative<Json::Integer>(value.value),
              name + ": unsupported scalar");
  }
}
Json::Object Markers(const fs::path &directory) {
  Json::Object values;
  for (const auto &file : fs::directory_iterator(directory)) {
    auto name = file.path().filename().string();
    auto at = name.find('=');
    if (at == std::string::npos)
      continue;
    auto split = name.find('=', at + 1);
    Require(split != std::string::npos &&
                name.find('=', split + 1) == std::string::npos,
            "malformed result marker");
    auto key = name.substr(0, at), tag = name.substr(at + 1, split - at - 1),
         text = name.substr(split + 1);
    Json value;
    if (tag == "bool") {
      Require(text == "0" || text == "1", "invalid boolean marker");
      value = Json(text == "1");
    } else if (tag == "int") {
      Require(std::regex_match(text, kDecimal), "noncanonical integer marker");
      value = Json(Json::Integer{text});
    } else {
      Require(tag == "string", "unknown marker type");
      value = Json(text);
    }
    Require(values.emplace(key, value).second, "duplicate answer: " + key);
  }
  Validate(values);
  return values;
}
void Publish(const fs::path &directory, const Json::Object &values) {
  Validate(values);
  fs::create_directories(directory);
  for (const auto &[name, value] : values) {
    std::string tag = "int", text = Scalar(value);
    if (std::holds_alternative<bool>(value.value)) {
      tag = "bool";
      text = value.boolean() ? "1" : "0";
    } else if (std::holds_alternative<std::string>(value.value))
      tag = "string";
    Write(directory / (name + '=' + tag + '=' + text), "");
  }
  Write(directory / "results.json", Dump(Json(values)) + '\n');
}
void PublishPayload(const fs::path &directory, const Json &value) {
  auto text = Dump(value);
  // The consumer pattern uses ASCII keys/values; reject raw non-ASCII rather
  // than silently emit bytes which Starlark's filename decoder cannot recover.
  for (unsigned char c : text)
    Require(c < 128, "structured payload must be ASCII-escaped JSON");
  Require(text.size() <= 1024 * 1024, "structured payload exceeds 1 MiB");
  const char *hex = "0123456789abcdef";
  std::string encoded;
  for (unsigned char c : text) {
    encoded += hex[c >> 4];
    encoded += hex[c & 15];
  }
  for (size_t i = 0; i * 180 < encoded.size(); ++i) {
    std::ostringstream name;
    name << std::setw(8) << std::setfill('0') << i << '='
         << encoded.substr(i * 180, 180);
    Write(directory / name.str(), "");
  }
  Write(directory / "payload.json", text + '\n');
}
std::string Substitute(const std::string &text, const Json &tc,
                       const std::string &work, const Json::Object &answers) {
  std::string result;
  for (size_t i = 0; i < text.size();) {
    if (text[i] == '{') {
      auto end = text.find('}', i + 1);
      if (end != std::string::npos) {
        auto token = text.substr(i + 1, end - i - 1);
        std::string replacement;
        bool matched = true;
        if (token == "cc" || token == "ld" || token == "ar")
          replacement = tc.at(token).string();
        else if (token == "work")
          replacement = work;
        else if (token.rfind("result:", 0) == 0) {
          auto found = answers.find(token.substr(7));
          Require(found != answers.end(),
                  "unresolved substitution: {" + token + '}');
          replacement = Scalar(found->second);
        } else {
          Require(token != "compile_flags" && token != "link_flags",
                  "unresolved reserved substitution: {" + token + '}');
          matched = false;
        }
        if (matched) {
          result += replacement;
          i = end + 1;
          continue;
        }
      }
    }
    result += text[i++];
  }
  return result;
}
std::string Runfile(const std::string &argv0, const std::string &suffix) {
  std::vector<fs::path> roots{argv0 + ".runfiles"};
  if (const char *dir = std::getenv("RUNFILES_DIR"))
    roots.emplace_back(dir);
  if (const char *dir = std::getenv("TEST_SRCDIR"))
    roots.emplace_back(dir);
  for (const auto &root : roots) {
    if (!fs::exists(root))
      continue;
    for (const auto &repository : fs::directory_iterator(root)) {
      auto path = repository.path() / suffix;
      if (fs::exists(path))
        return fs::absolute(path).string();
    }
  }
  throw std::runtime_error("declared runfile unavailable: " + suffix);
}
} // namespace autoconf
