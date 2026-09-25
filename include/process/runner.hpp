#ifndef RUNNER_HPP
#define RUNNER_HPP

#include "process/status.hpp"

#include <string>
#include <cstddef>
#include <filesystem>
#include <chrono>

struct RunnerOptions {
  std::filesystem::path exe_path;
  std::filesystem::path work_dir;

  std::filesystem::path input_path;
  std::filesystem::path output_path;

  std::chrono::milliseconds time_limit{0};
  std::size_t memory_limit_bytes{0};
};

struct [[nodiscard]] RunnerResult {
  RunnerStatus status{RunnerStatus::SystemError};
  std::chrono::milliseconds cpu_time{0};
  std::chrono::milliseconds wall_time{0};
  std::size_t memory_bytes{0};
  std::string message;

  bool ok() const noexcept { return status == RunnerStatus::Success; }
  explicit operator bool() const noexcept {
    return status == RunnerStatus::Success;
  }
};

RunnerResult run_exe(const RunnerOptions &opts);

#endif // RUNNER_HPP