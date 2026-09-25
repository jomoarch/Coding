#include "io/config.hpp"
#include "toml.hpp"

#include <windows.h>

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

std::filesystem::path module_dir() {
  std::wstring buf(MAX_PATH, L'\0');
  for (;;) {
    const DWORD n = ::GetModuleFileNameW(nullptr, buf.data(),
                                         static_cast<DWORD>(buf.size()));
    if (n == 0)
      return {};
    if (n < buf.size()) {
      buf.resize(n);
      break;
    }
    if (buf.size() >= 32768)
      return {};
    buf.resize(buf.size() * 2);
  }
  return std::filesystem::path(buf).parent_path();
}

std::filesystem::path derive_probe_path(const std::filesystem::path &p) {
  return p.parent_path() /
         (p.stem().string() + ".probe" + p.extension().string());
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
    read_field(t, "output_probe", c.exe_path_probe);
    read_string_array(t, "args", c.args);
  });

  // [inject]
  read_section(tbl, "inject", [&](const toml::table &t) {
    read_field(t, "enabled", c.inject_probe);
    read_field(t, "header", c.inject_header);
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

  // [io]
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

  const auto base = std::filesystem::absolute(path).parent_path();
  resolve_all(base, c.source_path, c.exe_path, c.exe_path_probe, c.work_dir,
              c.input_dir, c.output_dir, c.single_input, c.single_output,
              c.inject_header);

  if (c.exe_path_probe.empty() && !c.exe_path.empty())
    c.exe_path_probe = derive_probe_path(c.exe_path);

  if (c.inject_probe && c.inject_header.empty()) {
    std::error_code ec;
    const auto local = base / "include" / "inject" / "probe.h";
    const auto tool = module_dir() / "include" / "inject" / "probe.h";
    if (std::filesystem::exists(local, ec))
      c.inject_header = local;
    else if (!tool.empty() && std::filesystem::exists(tool, ec))
      c.inject_header = tool;
    else
      c.inject_header = local;
  }

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

  if (c.inject_probe) {
    std::error_code ec;
    if (!std::filesystem::exists(c.inject_header, ec)) {
      r.message =
          "Config: injection header not found: " + c.inject_header.string() +
          " (set [inject].header, or [inject].enabled = false)";
      return r;
    }
    if (c.exe_path_probe == c.exe_path) {
      r.message = "Config: [compiler].output_probe must differ from "
                  "[compiler].output - the probe build injects a header that "
                  "cg_b must not see";
      return r;
    }
  } else {
    c.exe_path_probe = c.exe_path;
  }

  r.success = true;
  r.message = "OK";
  return r;
}