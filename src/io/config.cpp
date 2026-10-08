#include "io/config.hpp"
#include "base/text.hpp"
#include "base/path.hpp"
#include "toml.hpp"

#include <windows.h>

#include <algorithm>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string_view>
#include <system_error>
namespace coding {

namespace {

std::filesystem::path resolve(const std::filesystem::path &base,
                              const std::filesystem::path &p) {
  if (p.empty() || p.is_absolute())
    return compress_path(p);
  return compress_path(base / p);
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
  return compress_path(p.parent_path() /
                       (p.stem().string() + ".probe" + p.extension().string()));
}

std::filesystem::path read_pointer(const std::filesystem::path &link) {
  std::error_code ec;
  if (!std::filesystem::is_regular_file(link, ec))
    return {};

  std::ifstream file(link, std::ios::binary);
  if (!file)
    return {};

  std::string line;
  bool first = true;
  while (std::getline(file, line)) {
    if (first) {
      first = false;
      if (line.size() >= 3 && static_cast<unsigned char>(line[0]) == 0xEF &&
          static_cast<unsigned char>(line[1]) == 0xBB &&
          static_cast<unsigned char>(line[2]) == 0xBF)
        line.erase(0, 3);
    }
    if (!line.empty() && line.back() == '\r')
      line.pop_back();

    std::string_view trimmed = text::trim(line);
    if (trimmed.empty() || trimmed.front() == '#')
      continue;

    if (trimmed.size() >= 2 &&
        ((trimmed.front() == '"' && trimmed.back() == '"') ||
         (trimmed.front() == '\'' && trimmed.back() == '\'')))
      trimmed = text::trim(trimmed.substr(1, trimmed.size() - 2));
    if (trimmed.empty())
      continue;

    const std::filesystem::path target(trimmed);
    if (target.is_absolute())
      return compress_path(target);
    return compress_path(link.parent_path() / target);
  }
  return {};
}

struct LocatedConfig {
  std::filesystem::path path;
  std::string note;
  bool error{false};
};

bool is_link_file(const std::filesystem::path &p) {
  const std::string name = p.filename().string();
  return name.size() > 5 &&
         text::to_lower(name.substr(name.size() - 5)) == ".link";
}

std::string path_key(const std::filesystem::path &p) {
  std::filesystem::path canon = compress_path(p, true, true);
  return text::to_lower(canon.string());
}

std::string chain_text(const std::vector<std::string> &chain) {
  std::string out;
  for (const std::string &step : chain) {
    if (!out.empty())
      out += " -> ";
    out += step;
  }
  return out;
}

LocatedConfig follow_chain(const std::filesystem::path &start) {
  constexpr int kMaxHops = 16;

  LocatedConfig found;
  std::vector<std::string> chain;
  std::vector<std::string> seen;
  std::filesystem::path link = start;

  for (int hop = 0; hop < kMaxHops; ++hop) {
    chain.push_back(link.string());

    const std::string key = path_key(link);
    if (std::find(seen.begin(), seen.end(), key) != seen.end()) {
      found.error = true;
      found.note = "link loop: " + chain_text(chain);
      return found;
    }
    seen.push_back(key);

    const std::filesystem::path target = read_pointer(link);
    if (target.empty()) {
      found.error = true;
      found.note = link.string() + " is a link with no path in it";
      return found;
    }

    std::error_code ec;
    if (!std::filesystem::exists(target, ec)) {
      found.error = true;
      found.note = link.string() + " points at " + target.string() +
                   ", which is not there";
      return found;
    }
    if (std::filesystem::is_directory(target, ec)) {
      std::filesystem::path candidate = target / "config.toml";
      std::error_code cand_ec;
      if (std::filesystem::is_regular_file(candidate, cand_ec)) {
        std::error_code abs_ec;
        const std::filesystem::path clean =
            compress_path(candidate, abs_ec, false, true);
        found.path = abs_ec ? candidate : clean;
        found.note = "using " + found.path.string() + " (followed " +
                     chain_text(chain) + " ans found config.toml in directory)";
        return found;
      } else {
        found.error = true;
        found.note = link.string() + " points at " + target.string() +
                     ", which is a directory, but no config.toml found inside";
        return found;
      }
    }

    if (!is_link_file(target)) {
      std::error_code abs_ec;
      const std::filesystem::path clean =
          compress_path(target, abs_ec, false, true);
      found.path = abs_ec ? target : clean;
      found.note = "using " + found.path.string() + " (followed " +
                   chain_text(chain) + ")";
      return found;
    }

    link = target;
  }

  found.error = true;
  found.note = "link chain is deeper than " + std::to_string(kMaxHops) +
               " links: " + chain_text(chain);
  return found;
}

LocatedConfig locate_config(const std::filesystem::path &requested) {
  LocatedConfig found;

  std::error_code ec;
  if (std::filesystem::is_regular_file(requested, ec)) {
    if (is_link_file(requested))
      return follow_chain(requested);
    found.path = requested;
    return found;
  }

  const std::filesystem::path exe = module_dir();
  if (exe.empty())
    return found;

  const bool defaulted = !requested.is_absolute() &&
                         requested == std::filesystem::path("config.toml");
  if (defaulted) {
    const std::filesystem::path beside = compress_path(exe / "config.toml");
    if (std::filesystem::is_regular_file(beside, ec)) {
      found.path = beside;
      found.note = "using " + beside.string() + " (next to the executable)";
      return found;
    }
  }

  const std::filesystem::path link = compress_path(exe / "config.link");
  if (std::filesystem::is_regular_file(link, ec))
    return follow_chain(link);
  return found;
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

  const LocatedConfig located = locate_config(path);
  if (located.error) {
    r.message = located.note;
    return r;
  }
  if (located.path.empty()) {
    r.message = "Config not found: " + path.string() +
                " (and no config.toml or config.link next to the executable)";
    return r;
  }
  if (!located.note.empty())
    std::cerr << "[config] " << located.note << "\n";

  const std::filesystem::path &config = located.path;
  r.config.config_path = config;

  toml::table tbl;
  try {
    tbl = toml::parse_file(config.string());
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
    read_field(t, "force_rebuild", c.force_rebuild);
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
    read_field(t, "single_answer", c.single_answer);
    read_field(t, "result_root", c.result_root);
    read_field(t, "single_name", c.single_name);
    read_field(t, "single_max_count", c.single_max_count);
    read_field(t, "batch_max_count", c.batch_max_count);
    read_field(t, "trash_max_bytes", c.trash_max_bytes);
    read_field_as<bool>(t, "merge_stderr", c.merge_stderr,
                        [](bool v) { return v ? 1 : 0; });
    read_field(t, "answer_dir", c.answer_dir);
    read_field(t, "print_input", c.print_input);
    read_field(t, "colorize_output", c.colorize_output);
  });

  // [thread]
  read_section(tbl, "thread", [&](const toml::table &t) {
    read_field(t, "thread_max", c.thread_max);
  });

  const std::filesystem::path config_dir =
      compress_path(config, false, true).parent_path();
  std::string raw_base;
  if (auto v = tbl["base"].value<std::string>())
    raw_base = *v;

  std::filesystem::path base = config_dir;
  if (!raw_base.empty())
    base = resolve(config_dir, std::filesystem::path(raw_base));
  c.base = compress_path(base, false, true);

  resolve_all(base, c.source_path, c.exe_path, c.exe_path_probe, c.work_dir,
              c.input_dir, c.output_dir, c.single_input, c.single_output,
              c.single_answer, c.result_root, c.answer_dir, c.inject_header);

  if (c.exe_path_probe.empty() && !c.exe_path.empty())
    c.exe_path_probe = derive_probe_path(c.exe_path);

  if (c.inject_probe && c.inject_header.empty()) {
    std::error_code ec;
    const auto at_base = base / "include" / "inject" / "probe.h";
    const auto at_config = config_dir / "include" / "inject" / "probe.h";
    const auto at_tool = module_dir() / "include" / "inject" / "probe.h";
    if (std::filesystem::exists(at_base, ec))
      c.inject_header = compress_path(at_base);
    else if (std::filesystem::exists(at_config, ec))
      c.inject_header = compress_path(at_config);
    else if (!at_tool.empty() && std::filesystem::exists(at_tool, ec))
      c.inject_header = compress_path(at_tool);
    else
      c.inject_header = compress_path(at_base);
  }

  if (!c.inject_probe)
    c.exe_path_probe = compress_path(c.exe_path);

  r.success = true;
  r.message = "OK";
  return r;
}

} // namespace coding