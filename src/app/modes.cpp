#include "app/modes.hpp"

#include "app/builder.hpp"
#include "app/formatter.hpp"
#include "io/iofile.hpp"
#include "process/runner_batch.hpp"
#include "process/runner_single.hpp"
#include "base/color.hpp"
#include "base/text.hpp"

#include <windows.h>

#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <utility>

namespace fs = std::filesystem;

namespace {

BuildResult build_step(const AppConfig &cfg) {
  BuildOption opt;
  opt.source_path = cfg.source_path;
  opt.output_path = cfg.exe_path;
  opt.args = cfg.args;
  opt.extra_deps.push_back(cfg.config_path);
  opt.force_rebuild = cfg.force_rebuild;

  const BuildResult br = ensure_built(opt);

  if (!br)
    std::cerr << color::err("[compile] Failed:\n", br.message) << "\n";
  return br;
}

void print_build_line(const AppConfig &cfg, const BuildResult &s) {
  color::Scope red(std::cout, {color::Code::Green, color::Code::Bold});
  if (s.rebuilt)
    std::cout << "[build] built -> " << cfg.exe_path.string() << '\n';
  else
    std::cout << "[build] up-to-date ("
              << cfg.exe_path.filename().string() + ")\n";
}

std::string trim_lower(std::string_view s) {
  return text::to_lower(text::trim(s));
}

bool write_text(const fs::path &path, const std::string &text) {
  std::error_code ec;
  if (!path.parent_path().empty())
    fs::create_directories(path.parent_path(), ec);

  std::ofstream f(path, std::ios::binary | std::ios::trunc);
  if (!f)
    return false;
  f.write(text.data(), static_cast<std::streamsize>(text.size()));
  return f.good();
}

bool ask_save(const fs::path &target) {
  HANDLE hIn = GetStdHandle(STD_INPUT_HANDLE);
  DWORD mode = 0;
  if (hIn != nullptr && hIn != INVALID_HANDLE_VALUE &&
      GetConsoleMode(hIn, &mode))
    FlushConsoleInputBuffer(hIn);

  std::cout << "[save] Save output to " << target.string() << " ? [y/N] "
            << std::flush;

  std::string line;
  if (!std::getline(std::cin, line)) {
    std::cout << "\n" << color::info("[save] skipped (stdin closed)") << "\n";
    return false;
  }

  const std::string ans = trim_lower(line);
  if (ans != "y" && ans != "yes") {
    std::cout << color::info("[save] skipped") << "\n";
    return false;
  }
  return true;
}

inline std::string bg(std::string_view s) {
  return color::paint(s, {color::Code::BgWhite, color::Code::Black});
}

int finish_single(const AppConfig &cfg, const SingleRunResult &run) {
  auto status = [&]() -> std::string {
    switch (run.result.status) {
    case RunnerStatus::Success:
      return color::ok("success");
    case RunnerStatus::TimeLimitExceeded:
      return color::err("TLE");
    case RunnerStatus::MemoryLimitExceeded:
      return color::err("MLE");
    case RunnerStatus::RuntimeError:
      return color::err("runtime error");
    default:
      return color::warn(run.result.message);
    }
  };

  std::ostringstream oss;

  oss << "[result] cpu "
      << bg(std::to_string(run.result.cpu_time.count()) + " ms") << " wall "
      << bg(std::to_string(run.result.wall_time.count()) + " ms") << " mem "
      << bg(format_memory(run.result.memory_bytes)) << "  " << status();

  std::string out = oss.str();
  const std::size_t w = color::visible_width(out);
  std::cout << '\n' << std::string(w, '-') << '\n' << out << '\n';

  if (cfg.single_output.empty()) {
    std::cout << "[save] [io].single_output is not configured, nothing saved\n";
  } else if (ask_save(cfg.single_output)) {
    if (write_text(cfg.single_output, run.captured)) {
      std::cout << color::ok("[save] saved ", run.captured.size(),
                             " byte(s) -> ", cfg.single_output)
                << "\n";
    } else {
      std::cerr << color::err("[save] Failed to write ", cfg.single_output)
                << "\n";
      return 2;
    }
  }

  return run.result.status == RunnerStatus::Success ? 0 : 1;
}

} // namespace

int run_interactive(const AppConfig &cfg) {
  const BuildResult built = build_step(cfg);
  if (!built)
    return 2;
  print_build_line(cfg, built);

  std::cout << "[run] interactive mode\n";

  SingleRunOption opt;
  opt.exe_path = cfg.exe_path;
  opt.work_dir = cfg.work_dir;
  opt.stdin_from_console = true;
  opt.echo = true;
  opt.colorize_output = cfg.colorize_output;

  return finish_single(cfg, run_single(opt));
}

int run_single_file(const AppConfig &cfg) {
  if (cfg.single_input.empty()) {
    std::cerr << color::err("[io] [io].single_input is required in -s mode")
              << "\n";
    return 2;
  }

  std::ifstream in(cfg.single_input, std::ios::binary);
  if (!in) {
    std::cerr << color::err("[io] Cannot open single_input: ", cfg.single_input)
              << "\n";
    return 2;
  }
  std::ostringstream ss;
  ss << in.rdbuf();
  const std::string input = ss.str();

  const BuildResult built = build_step(cfg);
  if (!built)
    return 2;
  print_build_line(cfg, built);

  const std::size_t input_size = input.size();
  std::cout << color::ok("[io] input <- ", cfg.single_input, " (", input_size,
                         input_size == 1 ? " byte" : " bytes", ")")
            << "\n\n--- output ---\n";

  SingleRunOption opt;
  opt.exe_path = cfg.exe_path;
  opt.work_dir = cfg.work_dir;
  opt.stdin_from_console = false;
  opt.input_text = input;
  opt.echo = true;
  opt.colorize_output = cfg.colorize_output;

  return finish_single(cfg, run_single(opt));
}

int run_batch(const AppConfig &cfg) {
  std::cout << "Config: " << cfg.config_path.string() << "\n"
            << "  source    : " << cfg.source_path.string() << "\n"
            << "  exe       : " << cfg.exe_path.string() << "\n"
            << "  input_dir : " << cfg.input_dir.string() << "\n"
            << "  output_dir: " << cfg.output_dir.string() << "\n"
            << "  args      : ";
  for (const auto &a : cfg.args)
    std::cout << '"' << a << "\" ";
  std::cout << "\n"
            << "  time      : " << cfg.time_limit.count() << " ms\n"
            << "  memory    : " << (cfg.memory_limit_bytes >> 20) << " MB\n";

  std::cout << "\n[1/3] Compiling...\n";
  const BuildResult built = build_step(cfg);
  if (!built)
    return 2;
  if (built.rebuilt)
    std::cout << "      rebuild: source/config newer than output\n"
              << color::ok("      OK -> ", cfg.exe_path) << "\n";
  else
    std::cout << "      skipped: up-to-date ("
              << cfg.exe_path.filename().string() << ")\n";

  std::cout << "\n[2/3] Preparing test cases...\n";
  IOFileOption io_opts;
  io_opts.input_dir = cfg.input_dir;
  io_opts.output_dir = cfg.output_dir;

  auto io_res = gen_filepair(io_opts);
  if (!io_res) {
    std::cerr << color::err("[io] ", io_res.message) << "\n";
    return 2;
  }
  if (io_res.pairs.empty()) {
    std::cerr << color::err("[io] No .in files found in ", cfg.input_dir)
              << "\n";
    return 2;
  }
  std::cout << color::info("      Found ", io_res.pairs.size(), " test case(s)")
            << "\n";

  std::cout << "\n[3/3] Running...\n";
  BatchOptions batch;
  batch.exe_path = cfg.exe_path;
  batch.work_dir = cfg.work_dir;
  batch.pairs = std::move(io_res.pairs);
  batch.time_limit = cfg.time_limit;
  batch.memory_limit_bytes = cfg.memory_limit_bytes;
  batch.thread_max = cfg.thread_max;

  auto results = run_all(batch);

  std::size_t clean = 0;
  for (const auto &u : results) {
    if (u.result.status == RunnerStatus::Success)
      ++clean;
  }

  std::cout << "\n";
  for (const auto &line : format_results(results, true, true, std::cout)) {
    std::cout << line << "\n";
  }
  std::cout << "\n"
            << clean << " / " << results.size() << " finished without error\n";

  return clean == results.size() ? 0 : 1;
}
