#include "config.hpp"
#include "modes.hpp"

#include <windows.h>

#include <filesystem>
#include <iostream>
#include <string>
#include <conio.h>

namespace {

enum class Mode { Batch, Single, Interactive };

class ConsoleUtf8Scope {
public:
  ConsoleUtf8Scope() {
    HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
    DWORD mode = 0;
    if (hOut == nullptr || hOut == INVALID_HANDLE_VALUE ||
        !GetConsoleMode(hOut, &mode))
      return;

    out_cp_ = GetConsoleOutputCP();
    in_cp_ = GetConsoleCP();
    if (out_cp_ != CP_UTF8)
      SetConsoleOutputCP(CP_UTF8);
    if (in_cp_ != CP_UTF8)
      SetConsoleCP(CP_UTF8);
  }

  ~ConsoleUtf8Scope() {
    std::cout.flush();
    std::cerr.flush();
    if (out_cp_ != 0 && out_cp_ != CP_UTF8)
      SetConsoleOutputCP(out_cp_);
    if (in_cp_ != 0 && in_cp_ != CP_UTF8)
      SetConsoleCP(in_cp_);
  }

  ConsoleUtf8Scope(const ConsoleUtf8Scope &) = delete;
  ConsoleUtf8Scope &operator=(const ConsoleUtf8Scope &) = delete;

private:
  UINT out_cp_{0};
  UINT in_cp_{0};
};

void print_usage(const char *argv0) {
  std::cerr
      << "Usage: " << argv0 << " [config.toml] [mode]\n"
      << "\n"
      << "Modes (default: -b):\n"
      << "  -i, --interactive  终端直接输入输出，结束后询问是否保存输出\n"
      << "  -s, --single       从 [io].single_input 读输入并打印，结束后询问\n"
      << "                     是否保存输出到 [io].single_output\n"
      << "  -b, --batch        批量跑 [io].input_dir 下的用例，写入 "
         "[io].output_dir\n"
      << "\n"
      << "Options:\n"
      << "  -h, --help         显示本帮助\n"
      << "\n"
      << "Exit codes: 0 = success, 1 = run failed, 2 = tool/system error\n";
}

} // namespace

int main(int argc, char **argv) {
  const ConsoleUtf8Scope console_utf8;

  std::filesystem::path config_path = "config.toml";
  Mode mode = Mode::Batch;

  for (int i = 1; i < argc; ++i) {
    const std::string arg = argv[i];
    if (arg == "-h" || arg == "--help") {
      print_usage(argv[0]);
      return 0;
    }
    if (arg == "-i" || arg == "--interactive") {
      mode = Mode::Interactive;
      continue;
    }
    if (arg == "-s" || arg == "--single") {
      mode = Mode::Single;
      continue;
    }
    if (arg == "-b" || arg == "--batch") {
      mode = Mode::Batch;
      continue;
    }
    if (!arg.empty() && arg.front() == '-') {
      std::cerr << "Unknown option: " << arg << "\n\n";
      print_usage(argv[0]);
      return 2;
    }
    config_path = arg;
  }

  auto cfg_res = load_config(config_path);
  if (!cfg_res.success) {
    std::cerr << "[config] " << cfg_res.message << "\n";
    return 2;
  }

  int code;
  switch (mode) {
  case Mode::Interactive:
    code = run_interactive(cfg_res.config);
    break;
  case Mode::Single:
    code = run_single_file(cfg_res.config);
    break;
  case Mode::Batch:
    code = run_batch(cfg_res.config);
    break;
  }

  std::cout << "\nPress any key to exit ...";
  _getch();

  return code;
}
