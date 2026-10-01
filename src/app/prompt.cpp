#include "app/prompt.hpp"

#include "base/color.hpp"
#include "base/text.hpp"

#include <windows.h>
#include <conio.h>

#include <iostream>
#include <string>

namespace prompt {

bool parse(int argc, char **argv, Options &out, std::string &error) {
  bool config_set = false;

  for (int i = 1; i < argc; ++i) {
    const std::string arg = argv[i];

    if (arg == "-h" || arg == "--help") {
      out.help = true;
      return true;
    }
    if (arg == "--pause") {
      out.pause = 1;
      continue;
    }
    if (arg == "--no-pause") {
      out.pause = 0;
      continue;
    }
    if (arg == "-c" || arg == "--config") {
      if (i + 1 >= argc) {
        error = "missing value for " + arg;
        return false;
      }
      out.config_path = argv[++i];
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
    out.config_path = arg;
    config_set = true;
  }
  return true;
}

void print_usage(const char *binary, const char *summary) {
  std::cout << binary << " - " << summary << "\n\n"
            << "Usage: " << binary << " [options] [config.toml]\n\n"
            << "Options:\n"
            << "  -c, --config <path>  config file to load (default: "
               "config.toml)\n"
            << "      --pause          always wait for a key press before "
               "exiting\n"
            << "      --no-pause       never wait for a key press\n"
            << "  -h, --help           show this help\n";
}

bool should_pause(int pause) noexcept {
  if (pause >= 0)
    return pause != 0;

  DWORD list[4] = {};
  const DWORD count = GetConsoleProcessList(list, 4);
  return count <= 1;
}

void pause_if_needed(int pause) {
  if (!should_pause(pause))
    return;
  std::cout << "\nPress any key to exit ..." << std::flush;
  _getch();
}

bool ask_save(const std::filesystem::path &target, std::string_view what) {
  HANDLE hIn = GetStdHandle(STD_INPUT_HANDLE);
  DWORD mode = 0;
  if (hIn != nullptr && hIn != INVALID_HANDLE_VALUE &&
      GetConsoleMode(hIn, &mode))
    FlushConsoleInputBuffer(hIn);

  std::cout << "[save] Save " << what << " to " << target.string()
            << " ? [y/N] " << std::flush;

  std::string line;
  if (!std::getline(std::cin, line)) {
    std::cout << "\n" << color::info("[save] skipped (stdin closed)") << "\n";
    return false;
  }

  const std::string answer = text::to_lower(text::trim(line));
  if (answer != "y" && answer != "yes") {
    std::cout << color::info("[save] skipped") << "\n";
    return false;
  }
  return true;
}

} // namespace prompt
