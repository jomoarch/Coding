#include "base/cleaner.hpp"
#include "base/color.hpp"
#include "io/config.hpp"

#include <filesystem>
#include <iostream>
#include <string>

namespace {

const char *kBinaryName = "cg_clean";

void print_usage() {
  std::cout
      << kBinaryName
      << " - remove contents of [io].input_dir and [io].output_dir\n\n"
      << "Usage: " << kBinaryName << " [options] [config.toml]\n\n"
      << "Options:\n"
      << "  -c, --config <path>  config file to load (default: config.toml)\n"
      << "  -h, --help           show this help\n";
}

bool clean_one(const std::filesystem::path &dir, const std::string &suf) {
  std::error_code ec;
  if (!std::filesystem::exists(dir, ec)) {
    std::cout << color::info("[clean] ", dir, " does not exist, skipped")
              << "\n";
    return true;
  }
  if (!std::filesystem::is_directory(dir, ec)) {
    std::cerr << color::err("[clean] ", dir, " is not a directory") << "\n";
    return false;
  }
  CleanerResult r = clean_dir(dir, suf);
  if (!r) {
    std::cerr << color::err("[clean] ", r.message) << "\n";
    return false;
  }
  std::cout << color::ok("[clean] removed contents of ", dir) << "\n";
  return true;
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

  if (!clean_one(cfg.input_dir, ".in"))
    return 1;
  if (!clean_one(cfg.output_dir, ""))
    return 1;

  return 0;
}