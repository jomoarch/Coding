#include "builder.hpp"
#include "compiler.hpp"

#include <filesystem>

BuildResult ensure_built(const BuildOption &opts) {
  BuildResult r;
  r.success = false;

  std::error_code ec;
  bool need =
      opts.force_rebuild || !std::filesystem::exists(opts.output_path, ec);

  if (!need) {
    auto exe_time = std::filesystem::last_write_time(opts.output_path, ec);
    if (ec) {
      r.message = "Cannot stat exe: " + ec.message();
      return r;
    }

    auto newer_than = [&](const std::filesystem::path &p) -> bool {
      if (p.empty() || !std::filesystem::exists(p, ec))
        return false;
      auto t = std::filesystem::last_write_time(p, ec);
      if (ec)
        return false;
      return t > exe_time;
    };

    if (newer_than(opts.source_path))
      need = true;
    else {
      for (const auto &d : opts.extra_deps) {
        if (newer_than(d)) {
          need = true;
          break;
        }
      }
    }
  }

  if (!need) {
    r.success = true;
    r.rebuilt = false;
    r.message = "up-to-date";
    return r;
  }

  CompilerOptions co;
  co.source_path = opts.source_path;
  co.output_path = opts.output_path;
  co.args = opts.args;

  auto cr = compile_source(co);
  r.success = cr.success;
  r.rebuilt = true;
  r.message = cr.message;
  return r;
}