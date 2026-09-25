#ifndef RUNNER_SINGLE_HPP
#define RUNNER_SINGLE_HPP

#include "process/runner.hpp"

#include <filesystem>
#include <string>

struct SingleRunOption {
  std::filesystem::path exe_path;
  std::filesystem::path work_dir;

  bool stdin_from_console{false};
  std::string input_text;

  bool echo{true};
  bool colorize_output{false};

  bool tagged_stream{false};
};

struct SingleRunResult {
  RunnerResult result;
  std::string captured;
  std::string warning;

  bool ok() const noexcept { return result.ok(); }
  explicit operator bool() const noexcept { return result.ok(); }
};

[[nodiscard]] SingleRunResult run_single(const SingleRunOption &opts);

#endif // RUNNER_SINGLE_HPP
