#include "app/builder.hpp"
#include "app/prompt.hpp"
#include "app/stale_exe.hpp"
#include "base/color.hpp"
#include "process/compiler.hpp"

#include <filesystem>
#include <iostream>
#include <string>

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

  std::string note;
  stale_exe::Outcome cleared = stale_exe::clear(opts.output_path, false, note);
  if (cleared == stale_exe::Outcome::Blocked) {
    std::cout << color::err("[build] ", note, "\n") << std::flush;
    if (prompt::ask_yes_no(
            "Move it into " +
            (opts.output_path.parent_path() / ".trash").string() +
            " under its SHA-256 and carry on?")) {
      cleared = stale_exe::clear(opts.output_path, true, note);
      if (cleared == stale_exe::Outcome::Parked)
        std::cout << color::ok("[build] ", note, "\n") << std::flush;
    }
  } else if (cleared == stale_exe::Outcome::Parked) {
    std::cout << color::ok("[build] ", note, "\n") << std::flush;
  }

  if (cleared == stale_exe::Outcome::Blocked) {
    r.message = note + "\nThe build cannot continue: close whatever is using " +
                opts.output_path.filename().string() + " and try again.";
    if (!prompt::should_pause(-1) && prompt::interactive_stdin())
      prompt::pause_if_needed(1);
    return r;
  }

  CompilerOptions co;
  co.source_path = opts.source_path;
  co.output_path = opts.output_path;
  co.args = opts.args;
  co.inject_header = opts.inject_header;

  auto cr = compile_source(co);
  r.success = cr.success;
  r.rebuilt = true;
  r.message = cr.message;
  return r;
}
