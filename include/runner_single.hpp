#ifndef RUNNER_SINGLE_HPP
#define RUNNER_SINGLE_HPP

#include "runner.hpp"

#include <filesystem>
#include <string>

struct SingleRunOption {
  std::filesystem::path exe_path;
  std::filesystem::path work_dir;

  bool stdin_from_console{false};
  std::string input_text;

  bool echo{true};
  bool colorize_output{false};
};

struct SingleRunResult {
  RunnerResult result;
  std::string captured;
};

[[nodiscard]] SingleRunResult run_single(const SingleRunOption &opts);

#endif // RUNNER_SINGLE_HPP
