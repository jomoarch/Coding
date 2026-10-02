#include "app/manager.hpp"
#include "app/prompt.hpp"
#include "app/viewer.hpp"
#include "base/color.hpp"
#include "compare/compare.hpp"
#include "io/config.hpp"
#include "store/store.hpp"

#include <iostream>
#include <string>
#include <vector>

namespace {

const char *kBinaryName = "cmp_s";

std::string archive_name(const AppConfig &cfg) {
  if (!cfg.single_name.empty())
    return cfg.single_name;
  if (!cfg.single_input.empty())
    return cfg.single_input.stem().string();
  return cfg.single_output.stem().string();
}

const char *kOptions =
    "      --no-save        do not offer to archive the comparison\n";

} // namespace

int main(int argc, char **argv) {
  color::install();

  bool no_save = false;
  std::vector<char *> kept;
  kept.push_back(argv[0]);
  for (int i = 1; i < argc; ++i) {
    if (std::string(argv[i]) == "--no-save") {
      no_save = true;
      continue;
    }
    kept.push_back(argv[i]);
  }

  prompt::Options cli;
  std::string error;
  if (!prompt::parse(static_cast<int>(kept.size()), kept.data(), cli, error)) {
    std::cerr << color::err(kBinaryName, ": ", error) << "\n"
              << "Try '" << kBinaryName << " --help' for usage.\n";
    return 2;
  }

  if (cli.help) {
    prompt::print_usage(
        kBinaryName,
        "compare [io].single_output against [io].single_answer, then preview",
        kOptions);
    std::cout << "\nAfter the preview it offers to archive the comparison in "
                 "[io].result_root.\n"
                 "Exit codes: 0 identical, 1 differing, 2 config/IO error.\n";
    return 0;
  }

  ConfigResult cfg_res = load_config(cli.config_path);
  if (!cfg_res) {
    std::cerr << color::err("[config] ", cfg_res.message) << "\n";
    return 2;
  }
  const AppConfig &cfg = cfg_res.config;

  if (cfg.single_output.empty()) {
    std::cerr << color::err(
                     "[compare] [io].single_output is required: it is the file "
                     "produced by the program")
              << "\n";
    return 2;
  }
  if (cfg.single_answer.empty()) {
    std::cerr << color::err("[compare] [io].single_answer is required: it is "
                            "the expected answer")
              << "\n";
    return 2;
  }

  CompareOption opts;
  opts.output_path = cfg.single_output;
  opts.expect_path = cfg.single_answer;

  const CompareResult result = compare_output(opts);
  if (!result.success) {
    std::cerr << color::err("[compare] ", result.message) << "\n";
    prompt::pause_if_needed(cli.pause);
    return 2;
  }
  if (!result.warning.empty())
    std::cerr << color::warn("[compare] ", result.warning) << "\n";

  if (result.exact_match) {
    std::cout << color::ok("[compare] identical (", result.output_line_count,
                           " lines)")
              << "\n";
    prompt::pause_if_needed(cli.pause);
    return 0;
  }

  std::cout << color::info("[compare] ", result.unmatched_line_count,
                           " lines unmatched (output ",
                           result.output_line_count, ", expect ",
                           result.expect_line_count, "), previewing...")
            << "\n";
  int code = viewer::view(result) == 0 ? 1 : 2;

  if (!no_save) {
    if (cfg.result_root.empty()) {
      std::cout << "[save] [io].result_root is not configured, nothing "
                   "archived\n";
    } else {
      const std::string name = archive_name(cfg);
      if (prompt::ask_yes_no("[save] Archive this comparison as \"" + name +
                             "\" in " + (cfg.result_root / "single").string() +
                             " ?")) {
        const store::SaveOutcome saved = store::save_single(
            cfg.result_root,
            {name, &result, cfg.single_output, cfg.single_answer});
        if (!saved) {
          std::cerr << color::err("[save] ", saved.message) << "\n";
          code = 2;
        } else {
          std::string note;
          manager::enforce_limits(cfg, false, &note);
          std::cout << color::ok("[save] ",
                                 saved.reused ? "already archived, time "
                                                "refreshed: "
                                              : "archived: ",
                                 name, " -> ", saved.id)
                    << "\n";
          if (!note.empty())
            std::cout << color::info("[save] ", note) << "\n";
        }
      }
    }
  }

  prompt::pause_if_needed(cli.pause);
  return code;
}
