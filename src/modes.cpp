#include "modes.hpp"

#include "builder.hpp"
#include "formatter.hpp"
#include "iofile.hpp"
#include "runner_batch.hpp"
#include "runner_single.hpp"
#include "color.hpp"

#include <windows.h>

#include <algorithm>
#include <cctype>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

namespace fs = std::filesystem;

namespace {

struct BuildStep {
  bool ok{false};
  bool rebuilt{false};
};

BuildStep build_step(const AppConfig &cfg) {
  BuildOption opt;
  opt.source_path = cfg.source_path;
  opt.output_path = cfg.exe_path;
  opt.args = cfg.args;
  opt.extra_deps.push_back(cfg.config_path);

  const BuildResult br = ensure_built(opt);

  BuildStep s;
  s.ok = br.success;
  s.rebuilt = br.rebuilt;
  if (!s.ok)
    std::cerr << color::err("[compile] Failed:\n" + br.message) << "\n";
  return s;
}

void print_build_line(const AppConfig &cfg, const BuildStep &s) {
  if (s.rebuilt)
    std::cout << "[build] built -> " + cfg.exe_path.string() + '\n';
  else
    std::cout << "[build] up-to-date (" + cfg.exe_path.filename().string() +
                     ")\n";
}

std::string trim_lower(std::string s) {
  auto not_space = [](unsigned char c) { return !std::isspace(c); };
  s.erase(s.begin(), std::find_if(s.begin(), s.end(), not_space));
  s.erase(std::find_if(s.rbegin(), s.rend(), not_space).base(), s.end());
  std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) {
    return static_cast<char>(std::tolower(c));
  });
  return s;
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

#define bg(s) color::paint(s, {color::Code::BgWhite, color::Code::Black})

  oss << "\n[result] cpu " +
             bg(std::to_string(run.result.cpu_time.count()) + " ms") +
             " wall " +
             bg(std::to_string(run.result.wall_time.count()) + " ms") +
             " mem " + bg(format_memory(run.result.memory_bytes)) + "  " +
             status()
      << '\n';

#undef bg

  std::cout << '\n';

  std::string out = oss.str();
  std::cout << std::string(out.size(), '-');
  std::cout << out;

  if (cfg.single_output.empty()) {
    std::cout << "[save] [io].single_output is not configured, nothing saved\n";
  } else if (ask_save(cfg.single_output)) {
    if (write_text(cfg.single_output, run.captured)) {
      std::cout << color::ok(
                       "[save] saved " + std::to_string(run.captured.size()) +
                       std::string(" byte(s) -> ") + cfg.single_output.string())
                << "\n";
    } else {
      std::cerr << color::err("[save] Failed to write " +
                              cfg.single_output.string())
                << "\n";
      return 2;
    }
  }

  return run.result.status == RunnerStatus::Success ? 0 : 1;
}

} // namespace

int run_interactive(const AppConfig &cfg) {
  const BuildStep built = build_step(cfg);
  if (!built.ok)
    return 2;
  print_build_line(cfg, built);

  std::cout << "[run] interactive mode\n";

  SingleRunOption opt;
  opt.exe_path = cfg.exe_path;
  opt.work_dir = cfg.work_dir;
  opt.stdin_from_console = true;
  opt.echo = true;
  opt.colorize_output = true;

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
    std::cerr << color::err("[io] Cannot open single_input: " +
                            cfg.single_input.string())
              << "\n";
    return 2;
  }
  std::ostringstream ss;
  ss << in.rdbuf();
  const std::string input = ss.str();

  const BuildStep built = build_step(cfg);
  if (!built.ok)
    return 2;
  print_build_line(cfg, built);

  std::cout << "\n--- output ---\n";

  SingleRunOption opt;
  opt.exe_path = cfg.exe_path;
  opt.work_dir = cfg.work_dir;
  opt.stdin_from_console = false;
  opt.input_text = input;
  opt.echo = true;

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
  const BuildStep built = build_step(cfg);
  if (!built.ok)
    return 2;
  if (built.rebuilt)
    std::cout << "      rebuild: source/config newer than output\n"
              << color::ok("      OK -> " + cfg.exe_path.string()) << "\n";
  else
    std::cout << "      skipped: up-to-date ("
              << cfg.exe_path.filename().string() << ")\n";

  std::cout << "\n[2/3] Preparing test cases...\n";
  IOFileOption io_opts;
  io_opts.input_dir = cfg.input_dir;
  io_opts.output_dir = cfg.output_dir;

  auto io_res = gen_filepair(io_opts);
  if (!io_res.success) {
    std::cerr << color::err("[io] " + io_res.message) << "\n";
    return 2;
  }
  if (io_res.pairs.empty()) {
    std::cerr << color::err("[io] No .in files found in " +
                            cfg.input_dir.string())
              << "\n";
    return 2;
  }
  std::cout << color::info("      Found " +
                           std::to_string(io_res.pairs.size()) +
                           std::string(" test case(s)"))
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

  std::size_t passed = 0;
  for (const auto &u : results) {
    if (u.result.status == RunnerStatus::Success)
      ++passed;
  }

  std::cout << "\n";
  for (const auto &line : format_results(results, true, true)) {
    std::cout << line << "\n";
  }
  std::cout << "\n" << passed << " / " << results.size() << " passed\n";

  return passed == results.size() ? 0 : 1;
}
