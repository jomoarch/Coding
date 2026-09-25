#include "io/config.hpp"
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

template <class T>
void read_field(const toml::table &t, std::string_view key, T &out) {
  if constexpr (std::is_same_v<T, std::filesystem::path>) {
    if (auto v = t[key].value<std::string>())
      out = *v;
  } else {
    if (auto v = t[key].value<T>())
      out = *v;
  }
}

template <class T, class U, class F>
void read_field_as(const toml::table &t, std::string_view key, U &out,
                   F &&transform) {
  if (auto v = t[key].value<T>())
    out = transform(*v);
}

template <class F>
void read_section(const toml::table &tbl, std::string_view name, F &&fn) {
  if (auto *t = tbl[name].as_table())
    std::forward<F>(fn)(*t);
}

inline void read_string_array(const toml::table &t, std::string_view key,
                              std::vector<std::string> &out) {
  if (auto *arr = t[key].as_array()) {
    out.reserve(arr->size());
    for (const auto &elem : *arr)
      if (auto s = elem.value<std::string>())
        out.push_back(*s);
  }
}

template <class... Paths>
void resolve_all(const std::filesystem::path &base, Paths &...paths) {
  ((paths = resolve(base, paths)), ...);
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
  r.config.config_path = path;

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
  read_section(tbl, "compiler", [&](const toml::table &t) {
    read_field(t, "source", c.source_path);
    read_field(t, "output", c.exe_path);
    read_string_array(t, "args", c.args);
  });
  read_section(tbl, "compiler", [&](const toml::table &t) {
    read_field(t, "source", c.source_path);
    read_field(t, "output", c.exe_path);
    read_string_array(t, "args", c.args);
  });

  // [runner]
  read_section(tbl, "runner", [&](const toml::table &t) {
    read_field(t, "work_dir", c.work_dir);
    read_field_as<int64_t>(t, "time_limit_ms", c.time_limit, [](int64_t v) {
      return std::chrono::milliseconds(v);
    });
    read_field_as<int64_t>(
        t, "memory_limit_mb", c.memory_limit_bytes,
        [](int64_t v) { return static_cast<std::size_t>(v) * 1024 * 1024; });
  });
  read_section(tbl, "runner", [&](const toml::table &t) {
    read_field(t, "work_dir", c.work_dir);
    read_field_as<int64_t>(t, "time_limit_ms", c.time_limit, [](int64_t v) {
      return std::chrono::milliseconds(v);
    });
    read_field_as<int64_t>(
        t, "memory_limit_mb", c.memory_limit_bytes,
        [](int64_t v) { return static_cast<std::size_t>(v) * 1024 * 1024; });
  });

  // [io]
  read_section(tbl, "io", [&](const toml::table &t) {
    read_field(t, "input_dir", c.input_dir);
    read_field(t, "output_dir", c.output_dir);
    read_field(t, "single_input", c.single_input);
    read_field(t, "single_output", c.single_output);
    read_field(t, "colorize_output", c.colorize_output);
  });
  read_section(tbl, "io", [&](const toml::table &t) {
    read_field(t, "input_dir", c.input_dir);
    read_field(t, "output_dir", c.output_dir);
    read_field(t, "single_input", c.single_input);
    read_field(t, "single_output", c.single_output);
    read_field(t, "colorize_output", c.colorize_output);
  });

  // [thread]
  read_section(tbl, "thread", [&](const toml::table &t) {
    read_field(t, "thread_max", c.thread_max);
  });
  read_section(tbl, "thread", [&](const toml::table &t) {
    read_field(t, "thread_max", c.thread_max);
  });

  const auto base = std::filesystem::absolute(path).parent_path();
  resolve_all(base, c.source_path, c.exe_path, c.work_dir, c.input_dir,
              c.output_dir, c.single_input, c.single_output);
  resolve_all(base, c.source_path, c.exe_path, c.work_dir, c.input_dir,
              c.output_dir, c.single_input, c.single_output);

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