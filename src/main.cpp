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

struct RebuildCheck {
  bool needed{true};
  std::string reason;
};

RebuildCheck check_rebuild(const AppConfig &cfg,
                           const std::filesystem::path &config_path) {
  RebuildCheck r;

  std::error_code ec;
  if (!std::filesystem::is_regular_file(cfg.exe_path, ec)) {
    r.reason = "output missing (" + cfg.exe_path.filename().string() + ")";
    return r;
  }

  const auto exe_time = std::filesystem::last_write_time(cfg.exe_path, ec);
  if (ec) {
    r.reason = "output timestamp unreadable (" + ec.message() + ")";
    return r;
  }

  std::string reasons;
  auto is_newer = [&](const std::filesystem::path &p, const char *label) {
    std::error_code e;
    const auto t = std::filesystem::last_write_time(p, e);
    if (e)
      return;
    if (t > exe_time)
      reasons += std::string(reasons.empty() ? "" : ", ") + label + " modified";
  };

  is_newer(cfg.source_path, "source");
  is_newer(config_path, "config");

  if (reasons.empty()) {
    r.needed = false;
    r.reason = "up to date (" + cfg.exe_path.filename().string() + ")";
  } else {
    r.reason = reasons;
  }
  return r;
}

} // namespace

int main(int argc, char **argv) {
  fs::path config_path = "config.toml";

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

  std::cout << "\n[1/3] Compiling...\n";
  const RebuildCheck rebuild = check_rebuild(cfg, config_path);

  if (!rebuild.needed) {
    std::cout << "      skipped: " << rebuild.reason << "\n";
  } else {
    std::cout << "      rebuild: " << rebuild.reason << "\n";

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
  }

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