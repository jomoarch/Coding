#include "app/prompt.hpp"
#include "app/viewer.hpp"
#include "base/color.hpp"
#include "compare/compare.hpp"
#include "compare/serialize.hpp"
#include "io/config.hpp"

#include <filesystem>
#include <iostream>
#include <string>

namespace {

const char *kBinaryName = "cmp_s";

} // namespace

int main(int argc, char **argv) {
  color::install();

  prompt::Options cli;
  std::string error;
  if (!prompt::parse(argc, argv, cli, error)) {
    std::cerr << color::err(kBinaryName, ": ", error) << "\n"
              << "Try '" << kBinaryName << " --help' for usage.\n";
    return 2;
  }
  if (cli.help) {
    prompt::print_usage(
        kBinaryName,
        "compare [io].single_output against [io].single_answer, then preview");
    std::cout << "\nAfter the preview it offers to save the result to "
                 "[io].single_result.\n"
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

  if (cfg.single_result.empty()) {
    std::cout << "[save] [io].single_result is not configured, nothing saved\n";
  } else if (prompt::ask_save(cfg.single_result, "result")) {
    const SaveResult saved = save_compare_result(result, cfg.single_result);
    if (!saved) {
      std::cerr << color::err("[save] ", saved.message) << "\n";
      code = 2;
    } else {
      std::error_code ec;
      std::cout << color::ok("[save] saved ",
                             std::filesystem::file_size(cfg.single_result, ec),
                             " byte(s) -> ", cfg.single_result)
                << "\n";
    }
  }

  prompt::pause_if_needed(cli.pause);
  return code;
}
