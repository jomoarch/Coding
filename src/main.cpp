#include "compiler.hpp"
#include "config.hpp"
#include "formatter.hpp"
#include "iofile.hpp"
#include "runner_batch.hpp"

#include <filesystem>
#include <iostream>
#include <string>

namespace fs = std::filesystem;

namespace {

void print_usage(const char *argv0) {
  std::cerr << "Usage: " << argv0 << " [config.toml]\n";
}

} // namespace

int main(int argc, char **argv) {
  // 1) 解析命令行：可选传入配置文件路径，默认 "config.toml"
  fs::path config_path = "config.toml";
  if (argc >= 2) {
    std::string arg = argv[1];
    if (arg == "-h" || arg == "--help") {
      print_usage(argv[0]);
      return 0;
    }
    config_path = arg;
  }

  // 2) 加载配置
  auto cfg_res = load_config(config_path);
  if (!cfg_res.success) {
    std::cerr << "[config] " << cfg_res.message << "\n";
    return 1;
  }
  const AppConfig &cfg = cfg_res.config;

  std::cout << "Config: " << config_path << "\n"
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

  // 3) 编译
  std::cout << "\n[1/3] Compiling...\n";
  CompilerOptions comp_opts;
  comp_opts.source_path = cfg.source_path;
  comp_opts.output_path = cfg.exe_path;
  comp_opts.args = cfg.args;

  auto comp_res = compile_source(comp_opts);
  if (!comp_res.success) {
    std::cerr << "[compile] Failed:\n" << comp_res.message << "\n";
    return 1;
  }
  std::cout << "      OK -> " << cfg.exe_path.string() << "\n";

  // 4) 准备 IO
  std::cout << "\n[2/3] Preparing test cases...\n";
  IOFileOption io_opts;
  io_opts.input_dir = cfg.input_dir;
  io_opts.output_dir = cfg.output_dir;

  auto io_res = gen_filepair(io_opts);
  if (!io_res.success) {
    std::cerr << "[io] " << io_res.message << "\n";
    return 1;
  }
  if (io_res.pairs.empty()) {
    std::cerr << "[io] No .in files found in " << cfg.input_dir.string()
              << "\n";
    return 1;
  }
  std::cout << "      Found " << io_res.pairs.size() << " test case(s)\n";

  // 5) 并发执行
  std::cout << "\n[3/3] Running...\n";
  BatchOptions batch;
  batch.exe_path = cfg.exe_path;
  batch.work_dir = cfg.work_dir;
  batch.pairs = std::move(io_res.pairs);
  batch.time_limit = cfg.time_limit;
  batch.memory_limit_bytes = cfg.memory_limit_bytes;
  batch.thread_max = cfg.thread_max;

  auto results = run_all(batch);

  // 6) 统计 + 输出
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