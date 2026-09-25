#include "process/process_launcher.hpp"
#include "base/win_error.hpp"

#include <vector>

namespace {

HANDLE as_handle(void *p) { return reinterpret_cast<HANDLE>(p); }

void close_handle(void *&p) {
  if (p) {
    ::CloseHandle(reinterpret_cast<HANDLE>(p));
    p = nullptr;
  }
}

} // namespace

ChildProcess::~ChildProcess() { reset(); }

ChildProcess::ChildProcess(ChildProcess &&o) noexcept
    : job_(o.job_), process_(o.process_), thread_(o.thread_), start_(o.start_),
      opts_(std::move(o.opts_)) {
  o.job_ = o.process_ = o.thread_ = nullptr;
}

ChildProcess &ChildProcess::operator=(ChildProcess &&o) noexcept {
  if (this != &o) {
    reset();
    job_ = o.job_;
    process_ = o.process_;
    thread_ = o.thread_;
    start_ = o.start_;
    opts_ = std::move(o.opts_);
    o.job_ = o.process_ = o.thread_ = nullptr;
  }
  return *this;
}

void ChildProcess::reset() noexcept {
  close_handle(thread_);
  close_handle(process_);
  close_handle(job_);
}

bool ChildProcess::spawn(const ProcessOptions &opts, ChildProcess &out,
                         std::string &error) {
  std::error_code ec;
  if (!std::filesystem::exists(opts.exe_path, ec)) {
    error = "Executable not found: " + opts.exe_path.string();
    return false;
  }

  out.opts_ = opts;

  HANDLE hJob = CreateJobObjectW(nullptr, nullptr);
  if (!hJob) {
    error = "CreateJobObjectW failed: " + win::last_error_string();
    return false;
  }
  out.job_ = hJob;

  JOBOBJECT_EXTENDED_LIMIT_INFORMATION jeli{};
  jeli.BasicLimitInformation.LimitFlags =
      JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE | JOB_OBJECT_LIMIT_ACTIVE_PROCESS;
  jeli.BasicLimitInformation.ActiveProcessLimit = 1;
  if (opts.memory_limit_bytes > 0) {
    jeli.BasicLimitInformation.LimitFlags |= JOB_OBJECT_LIMIT_PROCESS_MEMORY;
    jeli.ProcessMemoryLimit =
        static_cast<SIZE_T>(opts.memory_limit_bytes * 1.2);
  }
  if (!SetInformationJobObject(hJob, JobObjectExtendedLimitInformation, &jeli,
                               sizeof(jeli))) {
    error = "SetInformationJobObject failed: " + win::last_error_string();
    out.reset();
    return false;
  }

  STARTUPINFOW si{};
  si.cb = sizeof(si);
  si.dwFlags = STARTF_USESTDHANDLES;
  si.hStdInput = opts.stdin_handle != INVALID_HANDLE_VALUE
                     ? opts.stdin_handle
                     : GetStdHandle(STD_INPUT_HANDLE);
  si.hStdOutput = opts.stdout_handle != INVALID_HANDLE_VALUE
                      ? opts.stdout_handle
                      : GetStdHandle(STD_OUTPUT_HANDLE);
  si.hStdError = opts.stderr_handle != INVALID_HANDLE_VALUE
                     ? opts.stderr_handle
                     : GetStdHandle(STD_ERROR_HANDLE);

  std::wstring cmd = L"\"" + opts.exe_path.wstring() + L"\"";
  std::vector<wchar_t> cmdBuf(cmd.begin(), cmd.end());
  cmdBuf.push_back(L'\0');

  std::wstring workdir;
  const wchar_t *cwd = nullptr;
  if (!opts.work_dir.empty()) {
    workdir = opts.work_dir.wstring();
    cwd = workdir.c_str();
  }

  DWORD flags = CREATE_SUSPENDED;
  if (opts.no_window)
    flags |= CREATE_NO_WINDOW;

  PROCESS_INFORMATION pi{};
  if (!CreateProcessW(nullptr, cmdBuf.data(), nullptr, nullptr, TRUE, flags,
                      nullptr, cwd, &si, &pi)) {
    error = "CreateProcessW failed: " + win::last_error_string();
    out.reset();
    return false;
  }

  out.process_ = pi.hProcess;
  out.thread_ = pi.hThread;

  if (!AssignProcessToJobObject(hJob, pi.hProcess)) {
    TerminateProcess(pi.hProcess, 1);
    WaitForSingleObject(pi.hProcess, INFINITE);
    error = "AssignProcessToJobObject failed: " + win::last_error_string();
    out.reset();
    return false;
  }

  out.start_ = std::chrono::steady_clock::now();
  ResumeThread(pi.hThread);
  return true;
}

ProcessResult ChildProcess::wait() {
  ProcessResult r;

  if (!process_) {
    r.message = "ChildProcess not spawned";
    return r;
  }

  HANDLE hProcess = as_handle(process_);
  HANDLE hJob = as_handle(job_);

  const auto time_limit = opts_.time_limit;
  const auto mem_limit = opts_.memory_limit_bytes;
  const ULONGLONG time_limit_100ns =
      static_cast<ULONGLONG>(time_limit.count()) * 10000ULL;
  const DWORD poll_ms = 10;

  bool time_out = false;
  bool mem_exceeded = false;

  while (true) {
    DWORD w = WaitForSingleObject(hProcess, poll_ms);
    if (w == WAIT_OBJECT_0)
      break;

    if (time_limit.count() > 0) {
      FILETIME c, e, k, u;
      if (GetProcessTimes(hProcess, &c, &e, &k, &u)) {
        ULARGE_INTEGER ku{}, uu{};
        ku.LowPart = k.dwLowDateTime;
        ku.HighPart = k.dwHighDateTime;
        uu.LowPart = u.dwLowDateTime;
        uu.HighPart = u.dwHighDateTime;
        ULONGLONG cpu_100ns = ku.QuadPart + uu.QuadPart;
        if (cpu_100ns > time_limit_100ns) {
          time_out = true;
          TerminateJobObject(hJob, 1);
          WaitForSingleObject(hProcess, INFINITE);
          break;
        }
      }

      auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                         std::chrono::steady_clock::now() - start_)
                         .count();
      if (elapsed > time_limit.count() * 2) {
        time_out = true;
        TerminateJobObject(hJob, 1);
        WaitForSingleObject(hProcess, INFINITE);
        break;
      }
    }

    if (mem_limit > 0) {
      JOBOBJECT_EXTENDED_LIMIT_INFORMATION meminfo{};
      if (QueryInformationJobObject(hJob, JobObjectExtendedLimitInformation,
                                    &meminfo, sizeof(meminfo), nullptr)) {
        if (meminfo.PeakJobMemoryUsed > mem_limit) {
          mem_exceeded = true;
          TerminateJobObject(hJob, 1);
          WaitForSingleObject(hProcess, INFINITE);
          break;
        }
      }
    }
  }

  auto t1 = std::chrono::steady_clock::now();
  r.wall_time =
      std::chrono::duration_cast<std::chrono::milliseconds>(t1 - start_);

  FILETIME c, e, k, u;
  if (GetProcessTimes(hProcess, &c, &e, &k, &u)) {
    ULARGE_INTEGER ku{}, uu{};
    ku.LowPart = k.dwLowDateTime;
    ku.HighPart = k.dwHighDateTime;
    uu.LowPart = u.dwLowDateTime;
    uu.HighPart = u.dwHighDateTime;
    r.cpu_time =
        std::chrono::milliseconds((ku.QuadPart + uu.QuadPart) / 10000ULL);
  }

  JOBOBJECT_EXTENDED_LIMIT_INFORMATION jinfo{};
  if (QueryInformationJobObject(hJob, JobObjectExtendedLimitInformation, &jinfo,
                                sizeof(jinfo), nullptr)) {
    r.memory_bytes = static_cast<std::size_t>(jinfo.PeakJobMemoryUsed);
  }

  GetExitCodeProcess(hProcess, &r.exit_code);

  if (time_out) {
    r.status = RunnerStatus::TimeLimitExceeded;
    r.message = "Time Limit Exceeded";
  } else if (mem_exceeded || (mem_limit > 0 && r.memory_bytes > mem_limit)) {
    r.status = RunnerStatus::MemoryLimitExceeded;
    r.message = "Memory Limit Exceeded";
  } else if (r.exit_code == 0) {
    r.status = RunnerStatus::Success;
    r.message = "Success";
  } else {
    r.status = RunnerStatus::RuntimeError;
    r.message = "Exit code: " + std::to_string(r.exit_code);
  }

  return r;
}