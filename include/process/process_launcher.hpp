#ifndef PROCESS_LAUNCHER_HPP
#define PROCESS_LAUNCHER_HPP

#include "process/status.hpp"

#include <windows.h>
#include <chrono>
#include <cstddef>
#include <filesystem>
#include <string>

struct ProcessOptions {
  std::filesystem::path exe_path;
  std::filesystem::path work_dir;

  HANDLE stdin_handle{INVALID_HANDLE_VALUE};
  HANDLE stdout_handle{INVALID_HANDLE_VALUE};
  HANDLE stderr_handle{INVALID_HANDLE_VALUE};

  std::chrono::milliseconds time_limit{0};
  std::size_t memory_limit_bytes{0};
  bool no_window{true};
};

struct ProcessResult {
  RunnerStatus status{RunnerStatus::SystemError};
  std::chrono::milliseconds cpu_time{0};
  std::chrono::milliseconds wall_time{0};
  std::size_t memory_bytes{0};
  DWORD exit_code{0};
  std::string message;
};

class ChildProcess {
public:
  ChildProcess() = default;
  ~ChildProcess();
  ChildProcess(ChildProcess &&) noexcept;
  ChildProcess &operator=(ChildProcess &&) noexcept;
  ChildProcess(const ChildProcess &) = delete;
  ChildProcess &operator=(const ChildProcess &) = delete;

  static bool spawn(const ProcessOptions &opts, ChildProcess &out,
                    std::string &error);

  ProcessResult wait();

private:
  void reset() noexcept;

  void *job_ = nullptr;
  void *process_ = nullptr;
  void *thread_ = nullptr;
  std::chrono::steady_clock::time_point start_{};
  ProcessOptions opts_{};
};

#endif // PROCESS_LAUNCHER_HPP