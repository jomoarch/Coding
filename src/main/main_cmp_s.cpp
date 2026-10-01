#include "app/viewer.hpp"
#include "base/color.hpp"
#include "compare/compare.hpp"
#include "io/config.hpp"

#include <filesystem>
#include <iostream>
#include <string>

namespace {

const char *kBinaryName = "cmp_s";

void print_usage() {
  std::cout << kBinaryName
            << " - compare [io].single_output against [io].single_answer\n\n"
            << "Usage: " << kBinaryName << " [options] [config.toml]\n\n"
            << "Options:\n"
            << "  -c, --config <path>  config file to load (default: "
               "config.toml)\n"
            << "  -h, --help           show this help\n\n"
            << "Exit codes: 0 identical, 1 differing, 2 config/IO error.\n";
}

} // namespace

int main(int argc, char **argv) {
  color::install();

  std::string config_path = "config.toml";

  for (int i = 1; i < argc; ++i) {
    const std::string arg = argv[i];
    if (arg == "-h" || arg == "--help") {
      print_usage();
      return 0;
    }
    if (arg == "-c" || arg == "--config") {
      if (i + 1 >= argc) {
        std::cerr << color::err(kBinaryName, ": missing value for ", arg)
                  << "\n";
        return 2;
      }
      config_path = argv[++i];
      continue;
    }
    if (!arg.empty() && arg[0] == '-') {
      std::cerr << color::err(kBinaryName, ": unknown option: ", arg) << "\n";
      return 2;
    }
    config_path = arg;
  }

  ConfigResult cfg_res = load_config(config_path);
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

  CompareResult result = compare_output(opts);
  if (!result.success) {
    std::cerr << color::err("[compare] ", result.message) << "\n";
    return 2;
  }
  if (!result.warning.empty())
    std::cerr << color::warn("[compare] ", result.warning) << "\n";

  if (result.exact_match) {
    std::cout << color::ok("[compare] identical (", result.output_line_count,
                           " lines)")
              << "\n";
    return 0;
  }

  std::cout << color::info("[compare] ", result.unmatched_line_count,
                           " lines unmatched (output ",
                           result.output_line_count, ", expect ",
                           result.expect_line_count, "), browsing...")
            << "\n";
  viewer::view(result);
  return 1;
}
