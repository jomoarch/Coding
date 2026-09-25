#include "process/runner.hpp"
#include "process/process_launcher.hpp"
#include "base/handle.hpp"

RunnerResult run_exe(const RunnerOptions &opts) {
  RunnerResult r;
  r.status = RunnerStatus::SystemError;

  std::error_code ec;
  if (!std::filesystem::exists(opts.exe_path, ec)) {
    r.message = "Executable not found: " + opts.exe_path.string();
    return r;
  }
  if (!opts.input_path.empty() &&
      !std::filesystem::exists(opts.input_path, ec)) {
    r.message = "Input file not found: " + opts.input_path.string();
    return r;
  }

  SECURITY_ATTRIBUTES sa{};
  sa.nLength = sizeof(sa);
  sa.bInheritHandle = TRUE;

  HandleGuard hIn, hOut;
  if (!opts.input_path.empty()) {
    HANDLE h = CreateFileW(opts.input_path.wstring().c_str(), GENERIC_READ,
                           FILE_SHARE_READ, &sa, OPEN_EXISTING,
                           FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) {
      r.message = "Failed to open input file";
      return r;
    }
    hIn.reset(h);
  }
  if (!opts.output_path.empty()) {
    HANDLE h = CreateFileW(opts.output_path.wstring().c_str(), GENERIC_WRITE,
                           FILE_SHARE_READ, &sa, CREATE_ALWAYS,
                           FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) {
      r.message = "Failed to open output file";
      return r;
    }
    hOut.reset(h);
  }

  ProcessOptions po;
  po.exe_path = opts.exe_path;
  po.work_dir = opts.work_dir;
  po.stdin_handle = hIn.valid() ? hIn.get() : INVALID_HANDLE_VALUE;
  po.stdout_handle = hOut.valid() ? hOut.get() : INVALID_HANDLE_VALUE;
  po.stderr_handle = hOut.valid() ? hOut.get() : INVALID_HANDLE_VALUE;
  po.time_limit = opts.time_limit;
  po.memory_limit_bytes = opts.memory_limit_bytes;
  po.no_window = true;

  ChildProcess proc;
  std::string err;
  if (!ChildProcess::spawn(po, proc, err)) {
    r.message = err;
    return r;
  }

  ProcessResult pr = proc.wait();

  r.status = pr.status;
  r.cpu_time = pr.cpu_time;
  r.wall_time = pr.wall_time;
  r.memory_bytes = pr.memory_bytes;
  r.message = pr.message;
  return r;
}