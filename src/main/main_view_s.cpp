#include "app/prompt.hpp"
#include "app/viewer.hpp"
#include "base/color.hpp"
#include "compare/serialize.hpp"
#include "io/config.hpp"

#include <iostream>
#include <string>

namespace {

const char *kBinaryName = "view_s";

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
        "render the comparison result saved in [io].single_result");
    std::cout << "\nThis only reads a result; it never compares anything.\n"
                 "Exit codes: 0 identical, 1 differing, 2 config/IO error.\n";
    return 0;
  }

  ConfigResult cfg_res = load_config(cli.config_path);
  if (!cfg_res) {
    std::cerr << color::err("[config] ", cfg_res.message) << "\n";
    return 2;
  }
  const AppConfig &cfg = cfg_res.config;

  if (cfg.single_result.empty()) {
    std::cerr << color::err(
                     "[view] [io].single_result is required: it is the saved "
                     "comparison result to render")
              << "\n";
    return 2;
  }

  const LoadResult loaded = load_compare_result(cfg.single_result);
  if (!loaded) {
    std::cerr << color::err("[view] ", loaded.message) << "\n";
    prompt::pause_if_needed(cli.pause);
    return 2;
  }

  const CompareResult &result = loaded.result;
  if (!result.success) {
    std::cerr << color::err("[view] that result is a failed comparison: ",
                            result.message)
              << "\n";
    prompt::pause_if_needed(cli.pause);
    return 2;
  }
  if (!result.warning.empty())
    std::cerr << color::warn("[view] ", result.warning) << "\n";

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
                           result.expect_line_count, "), rendering...")
            << "\n";
  const int rendered = viewer::view(result);

  prompt::pause_if_needed(cli.pause);
  return rendered == 0 ? 1 : 2;
}
