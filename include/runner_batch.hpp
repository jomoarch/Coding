#ifndef RUNNER_BATCH_HPP
#define RUNNER_BATCH_HPP

#include "iofile.hpp"
#include "runner.hpp"

#include <cstddef>
#include <chrono>
#include <filesystem>
#include <string>
#include <vector>

struct BatchOptions {
  std::filesystem::path exe_path;
  std::filesystem::path work_dir;
  std::vector<FilePair> pairs;
  std::chrono::milliseconds time_limit{0};
  std::size_t memory_limit_bytes{0};
  std::size_t thread_max{1};
};

struct ResultUnit {
  std::string name;
  RunnerResult result;

  bool ok() const noexcept { return result.ok(); }
  explicit operator bool() const noexcept { return result.ok(); }
};

[[nodiscard]] std::vector<ResultUnit> run_all(const BatchOptions &opts);

#endif // RUNNER_BATCH_HPP