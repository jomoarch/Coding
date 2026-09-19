#include "runner.hpp"
#include "handle.hpp"
#include "win_error.hpp"

#include <windows.h>
#include <vector>

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

  HandleGuard hJob(CreateJobObjectW(nullptr, nullptr));
  if (!hJob.valid()) {
    r.message = "CreateJobObjectW failed: " + win::last_error_string();
    return r;
  }

  JOBOBJECT_EXTENDED_LIMIT_INFORMATION jeli{};
  jeli.BasicLimitInformation.LimitFlags =
      JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE | JOB_OBJECT_LIMIT_ACTIVE_PROCESS;
  jeli.BasicLimitInformation.ActiveProcessLimit = 1;
  if (opts.memory_limit_bytes > 0) {
    jeli.BasicLimitInformation.LimitFlags |= JOB_OBJECT_LIMIT_PROCESS_MEMORY;
    jeli.ProcessMemoryLimit =
        static_cast<SIZE_T>(opts.memory_limit_bytes * 1.2);
  }
  if (!SetInformationJobObject(hJob.get(), JobObjectExtendedLimitInformation,
                               &jeli, sizeof(jeli))) {
    r.message = "SetInformationJobObject failed: " + win::last_error_string();
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

  STARTUPINFOW si{};
  si.cb = sizeof(si);
  if (hIn.valid() || hOut.valid()) {
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdInput = hIn.valid() ? hIn.get() : GetStdHandle(STD_INPUT_HANDLE);
    si.hStdOutput = hOut.valid() ? hOut.get() : GetStdHandle(STD_OUTPUT_HANDLE);
    si.hStdError = hOut.valid() ? hOut.get() : GetStdHandle(STD_ERROR_HANDLE);
  }

  std::wstring cmd = L"\"" + opts.exe_path.wstring() + L"\"";
  std::vector<wchar_t> cmdBuf(cmd.begin(), cmd.end());
  cmdBuf.push_back(L'\0');

  std::wstring workdir;
  const wchar_t *cwd = nullptr;
  if (!opts.work_dir.empty()) {
    workdir = opts.work_dir.wstring();
    cwd = workdir.c_str();
  }

  PROCESS_INFORMATION pi{};
  if (!CreateProcessW(nullptr, cmdBuf.data(), nullptr, nullptr, TRUE,
                      CREATE_SUSPENDED | CREATE_NO_WINDOW, nullptr, cwd, &si,
                      &pi)) {
    r.message = "CreateProcessW failed: " + win::last_error_string();
    return r;
  }
  HandleGuard hProcess(pi.hProcess);
  HandleGuard hThread(pi.hThread);

  if (!AssignProcessToJobObject(hJob.get(), pi.hProcess)) {
    TerminateProcess(pi.hProcess, 1);
    WaitForSingleObject(pi.hProcess, INFINITE);
    r.message = "AssignProcessToJobObject failed: " + win::last_error_string();
    return r;
  }
  ResumeThread(pi.hThread);

  auto t0 = std::chrono::steady_clock::now();

  const auto time_limit = opts.time_limit;
  const auto mem_limit = opts.memory_limit_bytes;
  const ULONGLONG time_limit_100ns =
      static_cast<ULONGLONG>(time_limit.count()) * 10000ULL;
  const DWORD poll_ms = 10;

  bool time_out = false;
  bool mem_exceeded = false;

  while (true) {
    DWORD w = WaitForSingleObject(pi.hProcess, poll_ms);
    if (w == WAIT_OBJECT_0)
      break;

    if (time_limit.count() > 0) {
      FILETIME c, e, k, u;
      if (GetProcessTimes(pi.hProcess, &c, &e, &k, &u)) {
        ULARGE_INTEGER ku{}, uu{};
        ku.LowPart = k.dwLowDateTime;
        ku.HighPart = k.dwHighDateTime;
        uu.LowPart = u.dwLowDateTime;
        uu.HighPart = u.dwHighDateTime;
        ULONGLONG cpu_100ns = ku.QuadPart + uu.QuadPart;
        if (cpu_100ns > time_limit_100ns) {
          time_out = true;
          TerminateJobObject(hJob.get(), 1);
          WaitForSingleObject(pi.hProcess, INFINITE);
          break;
        }
      }
    }

    if (time_limit.count() > 0) {
      auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                         std::chrono::steady_clock::now() - t0)
                         .count();
      if (elapsed > time_limit.count() * 2) {
        time_out = true;
        TerminateJobObject(hJob.get(), 1);
        WaitForSingleObject(pi.hProcess, INFINITE);
        break;
      }
    }

    if (mem_limit > 0) {
      JOBOBJECT_EXTENDED_LIMIT_INFORMATION meminfo{};
      if (QueryInformationJobObject(hJob.get(),
                                    JobObjectExtendedLimitInformation, &meminfo,
                                    sizeof(meminfo), nullptr)) {
        if (meminfo.PeakJobMemoryUsed > mem_limit) {
          mem_exceeded = true;
          TerminateJobObject(hJob.get(), 1);
          WaitForSingleObject(pi.hProcess, INFINITE);
          break;
        }
      }
    }
  }

  auto t1 = std::chrono::steady_clock::now();
  r.wall_time = std::chrono::duration_cast<std::chrono::milliseconds>(t1 - t0);

  FILETIME c, e, k, u;
  if (GetProcessTimes(pi.hProcess, &c, &e, &k, &u)) {
    ULARGE_INTEGER ku{}, uu{};
    ku.LowPart = k.dwLowDateTime;
    ku.HighPart = k.dwHighDateTime;
    uu.LowPart = u.dwLowDateTime;
    uu.HighPart = u.dwHighDateTime;
    r.cpu_time =
        std::chrono::milliseconds((ku.QuadPart + uu.QuadPart) / 10000ULL);
  }

  JOBOBJECT_EXTENDED_LIMIT_INFORMATION jinfo{};
  if (QueryInformationJobObject(hJob.get(), JobObjectExtendedLimitInformation,
                                &jinfo, sizeof(jinfo), nullptr)) {
    r.memory_bytes = static_cast<std::size_t>(jinfo.PeakJobMemoryUsed);
  }

  DWORD exit_code = 0;
  GetExitCodeProcess(pi.hProcess, &exit_code);

  if (time_out) {
    r.status = RunnerStatus::TimeLimitExceeded;
    r.message = "Time Limit Exceeded";
    return r;
  }
  if (mem_exceeded || (mem_limit > 0 && r.memory_bytes > mem_limit)) {
    r.status = RunnerStatus::MemoryLimitExceeded;
    r.message = "Memory Limit Exceeded";
    return r;
  }
  if (exit_code == 0) {
    r.status = RunnerStatus::Success;
    r.message = "Success";
    return r;
  }
  r.status = RunnerStatus::RuntimeError;
  r.message = "Exit code: " + std::to_string(exit_code);
  return r;
}