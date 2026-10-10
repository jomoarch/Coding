#ifndef RUNNER_SINGLE_HPP
#define RUNNER_SINGLE_HPP

#include "process/runner.hpp"

#include <cstddef>
#include <filesystem>
#include <functional>
#include <string>
namespace coding {

struct SingleRunOption {
  std::filesystem::path exe_path;
  std::filesystem::path work_dir;

  bool stdin_from_console{false};
  std::string input_text;

  bool echo{true};
  bool colorize_output{false};

  bool tagged_stream{false};

  bool merge_stderr{false};

  std::function<std::size_t(char *buf, std::size_t n)> stdin_source;
  std::function<void(const char *data, std::size_t n, bool is_stderr)>
      on_output;
  std::function<void(void *job, void *process)> on_started;
};

struct SingleRunResult {
  RunnerResult result;
  std::string captured;
  std::string warning;

  bool ok() const noexcept { return result.ok(); }
  explicit operator bool() const noexcept { return result.ok(); }
};

[[nodiscard]] SingleRunResult run_single(const SingleRunOption &opts);

} // namespace coding

#endif // RUNNER_SINGLE_HPP
