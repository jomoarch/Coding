#include "runner_single.hpp"

#include "handle.hpp"
#include "win_error.hpp"
#include "color.hpp"

#include <algorithm>
#include <chrono>
#include <functional>
#include <iostream>
#include <thread>
#include <vector>

namespace {

void pump_to_terminal(HANDLE hRead, bool echo, std::string &sink,
                      bool colorize) {
  const bool use_color = colorize && color::enabled();
  const char *kOn = use_color ? "\033[36m" : "";
  const char *kOff = use_color ? "\033[0m" : "";
  const bool has_on = (kOn[0] != '\0');

  std::vector<char> buf(4096);
  DWORD n = 0;
  while (ReadFile(hRead, buf.data(), static_cast<DWORD>(buf.size()), &n,
                  nullptr) &&
         n > 0) {
    sink.append(buf.data(), n);
    if (echo) {
      if (has_on)
        std::cout << kOn;
      std::cout.write(buf.data(), static_cast<std::streamsize>(n));
      if (has_on)
        std::cout << kOff;
      std::cout.flush();
    }
  }
}

} // namespace

SingleRunResult run_single(const SingleRunOption &opts) {
  SingleRunResult out;
  out.result.status = RunnerStatus::SystemError;

  std::error_code ec;
  if (!std::filesystem::exists(opts.exe_path, ec)) {
    out.result.message = "Executable not found: " + opts.exe_path.string();
    return out;
  }

  if (!opts.work_dir.empty()) {
    std::filesystem::create_directories(opts.work_dir, ec);
    if (ec) {
      out.result.message = "Failed to create work_dir: " + ec.message();
      return out;
    }
  }

  HandleGuard hJob(CreateJobObjectW(nullptr, nullptr));
  if (!hJob.valid()) {
    out.result.message = "CreateJobObjectW failed: " + win::last_error_string();
    return out;
  }

  JOBOBJECT_EXTENDED_LIMIT_INFORMATION jeli{};
  jeli.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
  if (!SetInformationJobObject(hJob.get(), JobObjectExtendedLimitInformation,
                               &jeli, sizeof(jeli))) {
    out.result.message =
        "SetInformationJobObject failed: " + win::last_error_string();
    return out;
  }

  SECURITY_ATTRIBUTES sa{};
  sa.nLength = sizeof(sa);
  sa.bInheritHandle = TRUE;

  HANDLE hOutReadRaw = INVALID_HANDLE_VALUE;
  HANDLE hOutWriteRaw = INVALID_HANDLE_VALUE;
  if (!CreatePipe(&hOutReadRaw, &hOutWriteRaw, &sa, 0)) {
    out.result.message = "CreatePipe failed: " + win::last_error_string();
    return out;
  }
  HandleGuard hOutRead(hOutReadRaw);
  HandleGuard hOutWrite(hOutWriteRaw);
  SetHandleInformation(hOutRead.get(), HANDLE_FLAG_INHERIT, 0);

  HandleGuard hStdinRead;
  HandleGuard hStdinWrite;
  HANDLE hChildStdin = INVALID_HANDLE_VALUE;
  bool stdin_is_console = false;

  if (opts.stdin_from_console) {
    hChildStdin = GetStdHandle(STD_INPUT_HANDLE);
    if (hChildStdin == nullptr || hChildStdin == INVALID_HANDLE_VALUE) {
      out.result.message = "No console stdin available";
      return out;
    }
    DWORD mode = 0;
    stdin_is_console = GetConsoleMode(hChildStdin, &mode) != FALSE;
    SetHandleInformation(hChildStdin, HANDLE_FLAG_INHERIT, HANDLE_FLAG_INHERIT);
  } else {
    HANDLE r = INVALID_HANDLE_VALUE;
    HANDLE w = INVALID_HANDLE_VALUE;
    if (!CreatePipe(&r, &w, &sa, 0)) {
      out.result.message =
          "CreatePipe(stdin) failed: " + win::last_error_string();
      return out;
    }
    hStdinRead.reset(r);
    hStdinWrite.reset(w);
    SetHandleInformation(hStdinWrite.get(), HANDLE_FLAG_INHERIT, 0);
    hChildStdin = hStdinRead.get();
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

  STARTUPINFOW si{};
  si.cb = sizeof(si);
  si.dwFlags = STARTF_USESTDHANDLES;
  si.hStdInput = hChildStdin;
  si.hStdOutput = hOutWrite.get();
  si.hStdError = hOutWrite.get();

  PROCESS_INFORMATION pi{};
  DWORD create_flags = CREATE_SUSPENDED;
  if (!stdin_is_console)
    create_flags |= CREATE_NO_WINDOW;

  if (!CreateProcessW(nullptr, cmdBuf.data(), nullptr, nullptr, TRUE,
                      create_flags, nullptr, cwd, &si, &pi)) {
    out.result.message = "CreateProcessW failed: " + win::last_error_string();
    return out;
  }
  HandleGuard hProcess(pi.hProcess);
  HandleGuard hThread(pi.hThread);

  if (!AssignProcessToJobObject(hJob.get(), pi.hProcess)) {
    TerminateProcess(pi.hProcess, 1);
    WaitForSingleObject(pi.hProcess, INFINITE);
    out.result.message =
        "AssignProcessToJobObject failed: " + win::last_error_string();
    return out;
  }

  hOutWrite.reset();
  hStdinRead.reset();

  const auto t0 = std::chrono::steady_clock::now();
  ResumeThread(pi.hThread);

  std::string captured;
  std::thread pump(pump_to_terminal, hOutRead.get(), opts.echo,
                   std::ref(captured), opts.colorize_output);

  if (hStdinWrite.valid()) {
    std::size_t left = opts.input_text.size();
    const char *p = opts.input_text.data();
    while (left > 0) {
      DWORD written = 0;
      const DWORD chunk =
          static_cast<DWORD>(std::min<std::size_t>(left, 64 * 1024));
      if (!WriteFile(hStdinWrite.get(), p, chunk, &written, nullptr) ||
          written == 0)
        break;
      p += written;
      left -= written;
    }
    hStdinWrite.reset();
  }

  WaitForSingleObject(pi.hProcess, INFINITE);
  pump.join();
  hOutRead.reset();

  const auto t1 = std::chrono::steady_clock::now();
  out.result.wall_time =
      std::chrono::duration_cast<std::chrono::milliseconds>(t1 - t0);

  FILETIME c, e, k, u;
  if (GetProcessTimes(pi.hProcess, &c, &e, &k, &u)) {
    ULARGE_INTEGER ku{}, uu{};
    ku.LowPart = k.dwLowDateTime;
    ku.HighPart = k.dwHighDateTime;
    uu.LowPart = u.dwLowDateTime;
    uu.HighPart = u.dwHighDateTime;
    out.result.cpu_time =
        std::chrono::milliseconds((ku.QuadPart + uu.QuadPart) / 10000ULL);
  }

  JOBOBJECT_EXTENDED_LIMIT_INFORMATION jinfo{};
  if (QueryInformationJobObject(hJob.get(), JobObjectExtendedLimitInformation,
                                &jinfo, sizeof(jinfo), nullptr)) {
    out.result.memory_bytes = static_cast<std::size_t>(jinfo.PeakJobMemoryUsed);
  }

  DWORD exit_code = 0;
  GetExitCodeProcess(pi.hProcess, &exit_code);

  if (exit_code == 0) {
    out.result.status = RunnerStatus::Success;
    out.result.message = "Success";
  } else {
    out.result.status = RunnerStatus::RuntimeError;
    out.result.message = "Exit code: " + std::to_string(exit_code);
  }

  out.captured = std::move(captured);
  return out;
}
