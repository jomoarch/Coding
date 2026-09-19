#include "app.hpp"

#include "color.hpp"
#include "config.hpp"
#include "modes.hpp"
#include "text.hpp"

#include <windows.h>

#include <conio.h>

#include <iostream>
#include <string>

namespace app {

namespace {

struct CliOptions {
  std::string config_path{"config.toml"};
  bool force_rebuild{false};

  int pause{-1};

  bool help{false};
};

void print_usage(Mode mode) {
  const char *name = binary_name(mode);
  std::cout << name << " - " << description(mode) << "\n\n"
            << "Usage: " << name << " [options] [config.toml]\n\n"
            << "Options:\n"
            << "  -c, --config <path>  config file to load (default: "
               "config.toml)\n"
            << "  -f, --force          rebuild even when the binary is up to "
               "date\n"
            << "      --pause          always wait for a key press before "
               "exiting\n"
            << "      --no-pause       never wait for a key press\n"
            << "  -h, --help           show this help\n";
}

bool parse_args(int argc, char **argv, CliOptions &cli, std::string &error) {
  bool config_set = false;

  for (int i = 1; i < argc; ++i) {
    const std::string arg = argv[i];

    if (arg == "-h" || arg == "--help") {
      cli.help = true;
      return true;
    }
    if (arg == "-f" || arg == "--force") {
      cli.force_rebuild = true;
      continue;
    }
    if (arg == "--pause") {
      cli.pause = 1;
      continue;
    }
    if (arg == "--no-pause") {
      cli.pause = 0;
      continue;
    }
    if (arg == "-c" || arg == "--config") {
      if (i + 1 >= argc) {
        error = "missing value for " + arg;
        return false;
      }
      cli.config_path = argv[++i];
      config_set = true;
      continue;
    }
    if (!arg.empty() && arg[0] == '-') {
      error = "unknown option: " + arg;
      return false;
    }
    if (config_set) {
      error = "unexpected extra argument: " + arg;
      return false;
    }
    cli.config_path = arg;
    config_set = true;
  }
  return true;
}

bool should_pause(const CliOptions &cli) {
  if (cli.pause >= 0)
    return cli.pause != 0;

  DWORD list[4] = {};
  const DWORD count = GetConsoleProcessList(list, 4);
  return count <= 1;
}

void use_utf8_console() noexcept { SetConsoleOutputCP(CP_UTF8); }

int dispatch(Mode mode, const AppConfig &cfg) {
  switch (mode) {
  case Mode::Batch:
    return run_batch(cfg);
  case Mode::Single:
    return run_single_file(cfg);
  case Mode::Interactive:
    return run_interactive(cfg);
  }
  std::cerr << color::err("error: no handler for this mode") << "\n";
  return 2;
}

} // namespace

const char *binary_name(Mode mode) noexcept {
  switch (mode) {
  case Mode::Batch:
    return "cg_b";
  case Mode::Single:
    return "cg_s";
  case Mode::Interactive:
    return "cg_i";
  }
  return "cg";
}

const char *description(Mode mode) noexcept {
  switch (mode) {
  case Mode::Batch:
    return "compile once and run every test case in [io].input_dir";
  case Mode::Single:
    return "compile and run once against [io].single_input";
  case Mode::Interactive:
    return "compile and run with the console attached to stdin";
  }
  return "";
}

int run(Mode mode, int argc, char **argv) {
  use_utf8_console();
  color::install();

  CliOptions cli;
  std::string error;
  if (!parse_args(argc, argv, cli, error)) {
    std::cerr << color::err(binary_name(mode), ": ", error) << "\n"
              << "Try '" << binary_name(mode) << " --help' for usage.\n";
    return 2;
  }
  if (cli.help) {
    print_usage(mode);
    return 0;
  }

  ConfigResult cfg_res = load_config(cli.config_path);
  if (!cfg_res.success) {
    std::cerr << color::err("[config] ", cfg_res.message) << "\n";
    return 2;
  }
  cfg_res.config.force_rebuild = cli.force_rebuild;

  const int code = dispatch(mode, cfg_res.config);

  if (should_pause(cli)) {
    std::cout << "\nPress any key to exit ..." << std::flush;
    _getch();
  }
  return code;
}

} // namespace app
