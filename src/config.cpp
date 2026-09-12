#include "config.hpp"
#include "toml.hpp"

#include <fstream>
#include <sstream>
#include <system_error>

namespace {

std::filesystem::path resolve(const std::filesystem::path &base,
                              const std::filesystem::path &p) {
  if (p.empty() || p.is_absolute())
    return p;
  return base / p;
}

} // namespace

ConfigResult load_config(const std::filesystem::path &path) {
  ConfigResult r;
  r.success = false;

  std::error_code ec;
  if (!std::filesystem::exists(path, ec)) {
    r.message = "Config not found: " + path.string();
    return r;
  }

  toml::table tbl;
  try {
    tbl = toml::parse_file(path.string());
  } catch (const toml::parse_error &e) {
    std::ostringstream oss;
    oss << "TOML parse error at line " << e.source().begin.line << ": "
        << e.description();
    r.message = oss.str();
    return r;
  }

  AppConfig &c = r.config;

  // [compiler]
  if (auto *t = tbl["compiler"].as_table()) {
    if (auto v = (*t)["source"].value<std::string>())
      c.source_path = *v;
    if (auto v = (*t)["output"].value<std::string>())
      c.exe_path = *v;
    if (auto *arr = (*t)["args"].as_array()) {
      for (const auto &elem : *arr) {
        if (auto s = elem.value<std::string>())
          c.args.push_back(*s);
      }
    }
  }

  // [runner]
  if (auto *t = tbl["runner"].as_table()) {
    if (auto v = (*t)["work_dir"].value<std::string>())
      c.work_dir = *v;
    if (auto v = (*t)["time_limit_ms"].value<int64_t>())
      c.time_limit = std::chrono::milliseconds(*v);
    if (auto v = (*t)["memory_limit_mb"].value<int64_t>())
      c.memory_limit_bytes = static_cast<std::size_t>(*v) * 1024 * 1024;
  }

  // [io]
  if (auto *t = tbl["io"].as_table()) {
    if (auto v = (*t)["input_dir"].value<std::string>())
      c.input_dir = *v;
    if (auto v = (*t)["output_dir"].value<std::string>())
      c.output_dir = *v;
  }

  // [thread]
  if (auto *t = tbl["thread"].as_table()) {
    if (auto v = (*t)["thread_max"].value<std::size_t>())
      c.thread_max = *v;
  }

  const auto base = std::filesystem::absolute(path).parent_path();
  c.source_path = resolve(base, c.source_path);
  c.exe_path = resolve(base, c.exe_path);
  c.work_dir = resolve(base, c.work_dir);
  c.input_dir = resolve(base, c.input_dir);
  c.output_dir = resolve(base, c.output_dir);

  if (c.source_path.empty()) {
    r.message = "Config: [compiler].source is required";
    return r;
  }
  if (c.exe_path.empty()) {
    r.message = "Config: [compiler].output is required";
    return r;
  }
  if (c.work_dir.empty()) {
    r.message = "Config: [runner].work_dir is required";
    return r;
  }
  if (c.input_dir.empty()) {
    r.message = "Config: [io].input_dir is required";
    return r;
  }
  if (c.output_dir.empty()) {
    r.message = "Config: [io].output_dir is required";
    return r;
  }

  r.success = true;
  r.message = "OK";
  return r;
}