#include "process/runner_single.hpp"

#include "base/handle.hpp"
#include "base/win_error.hpp"
#include "base/color.hpp"

#include <algorithm>
#include <chrono>
#include <cstring>
#include <functional>
#include <iostream>
#include <string_view>
#include <thread>
#include <vector>

namespace {

inline constexpr char kFramedStdout = '\x01';
inline constexpr char kFramedStderr = '\x02';

void write_styled(bool colorize, const color::Style *style, const char *data,
                  std::size_t n) {
  const std::string_view chunk(data, n);
  if (colorize && style)
    style->write(std::cout, chunk);
  else
    std::cout.write(chunk.data(), static_cast<std::streamsize>(chunk.size()));
  std::cout.flush();
}

void pump_both(HANDLE hOut, HANDLE hErr, bool echo, bool colorize,
               const color::Style *out_style, const color::Style *err_style,
               std::string *out_sink) {
  HANDLE handles[2] = {hOut, hErr};
  const color::Style *styles[2] = {out_style, err_style};
  bool alive[2] = {true, true};

  std::vector<char> buf(4096);

  while (alive[0] || alive[1]) {
    HANDLE wait_arr[2];
    int map[2];
    DWORD cnt = 0;
    for (int i = 0; i < 2; ++i) {
      if (alive[i]) {
        wait_arr[cnt] = handles[i];
        map[cnt] = i;
        ++cnt;
      }
    }
    if (cnt == 0)
      break;

    DWORD r = WaitForMultipleObjects(cnt, wait_arr, FALSE, INFINITE);
    if (r < WAIT_OBJECT_0 || r >= WAIT_OBJECT_0 + cnt)
      break;

    int i = map[r - WAIT_OBJECT_0];

    DWORD n = 0;
    if (!ReadFile(handles[i], buf.data(), static_cast<DWORD>(buf.size()), &n,
                  nullptr) ||
        n == 0) {
      alive[i] = false;
      continue;
    }

    if (i == 0 && out_sink)
      out_sink->append(buf.data(), n);

    if (!echo)
      continue;

    write_styled(colorize, styles[i], buf.data(), n);
  }
}

class TagSplitter {
public:
  TagSplitter(bool echo, bool colorize, const color::Style *out_style,
              const color::Style *err_style, std::string *out_sink)
      : echo_(echo), colorize_(colorize), out_style_(out_style),
        err_style_(err_style), out_sink_(out_sink) {}

  void feed(const char *data, std::size_t n) {
    bytes_ += n;
    std::size_t start = 0;
    for (std::size_t i = 0; i < n; ++i) {
      const char c = data[i];
      if (c != kFramedStdout && c != kFramedStderr)
        continue;
      if (i != start)
        emit(data + start, i - start);
      current_ = c;
      saw_tag_ = true;
      start = i + 1;
    }
    if (start != n)
      emit(data + start, n - start);
  }

  bool saw_tag() const noexcept { return saw_tag_; }
  std::size_t bytes() const noexcept { return bytes_; }

private:
  void emit(const char *data, std::size_t n) {
    if (current_ == kFramedStderr) {
      if (echo_)
        write_styled(colorize_, err_style_, data, n);
      return;
    }

    if (out_sink_)
      out_sink_->append(data, n);
    if (echo_)
      write_styled(colorize_, out_style_, data, n);
  }

  bool echo_;
  bool colorize_;
  const color::Style *out_style_;
  const color::Style *err_style_;
  std::string *out_sink_;
  char current_{kFramedStdout};
  bool saw_tag_{false};
  std::size_t bytes_{0};
};

void pump_tagged(HANDLE hFramed, HANDLE hRawErr, TagSplitter &splitter,
                 std::string *raw_err_sink) {
  HANDLE handles[2] = {hFramed, hRawErr};
  bool alive[2] = {true, true};

  std::vector<char> buf(4096);

  while (alive[0] || alive[1]) {
    HANDLE wait_arr[2];
    int map[2];
    DWORD cnt = 0;
    for (int i = 0; i < 2; ++i) {
      if (alive[i]) {
        wait_arr[cnt] = handles[i];
        map[cnt] = i;
        ++cnt;
      }
    }
    if (cnt == 0)
      break;

    DWORD r = WaitForMultipleObjects(cnt, wait_arr, FALSE, INFINITE);
    if (r < WAIT_OBJECT_0 || r >= WAIT_OBJECT_0 + cnt)
      break;

    const int i = map[r - WAIT_OBJECT_0];

    DWORD n = 0;
    if (!ReadFile(handles[i], buf.data(), static_cast<DWORD>(buf.size()), &n,
                  nullptr) ||
        n == 0) {
      alive[i] = false;
      continue;
    }

    if (i == 0)
      splitter.feed(buf.data(), n);
    else if (raw_err_sink)
      raw_err_sink->append(buf.data(), n);
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

  auto make_pipe = [&](HandleGuard &read_end, HandleGuard &write_end,
                       const char *what) {
    HANDLE r = INVALID_HANDLE_VALUE;
    HANDLE w = INVALID_HANDLE_VALUE;
    if (!CreatePipe(&r, &w, &sa, 0)) {
      out.result.message = std::string("CreatePipe(") + what +
                           ") failed: " + win::last_error_string();
      return false;
    }
    read_end.reset(r);
    write_end.reset(w);
    SetHandleInformation(read_end.get(), HANDLE_FLAG_INHERIT, 0);
    return true;
  };

  HandleGuard hOutRead;
  HandleGuard hOutWrite;
  HandleGuard hErrRead;
  HandleGuard hErrWrite;
  if (!make_pipe(hOutRead, hOutWrite, "stdout"))
    return out;
  if (!make_pipe(hErrRead, hErrWrite, "stderr"))
    return out;

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
  si.hStdError = hErrWrite.get();

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
  hErrWrite.reset();
  hStdinRead.reset();

  const auto t0 = std::chrono::steady_clock::now();
  ResumeThread(pi.hThread);

  const color::Style stdout_style({color::Code::Green});
  const color::Style stderr_style({color::Code::Red});

  std::string captured;
  std::string raw_err;
  TagSplitter splitter(opts.echo, opts.colorize_output, &stdout_style,
                       &stderr_style, &captured);

  std::thread pump;
  if (opts.tagged_stream)
    pump = std::thread(pump_tagged, hOutRead.get(), hErrRead.get(),
                       std::ref(splitter), &raw_err);
  else
    pump = std::thread(pump_both, hOutRead.get(), hErrRead.get(), opts.echo,
                       opts.colorize_output, &stdout_style, &stderr_style,
                       &captured);

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
  hErrRead.reset();

  if (opts.tagged_stream && !raw_err.empty() && opts.echo)
    write_styled(opts.colorize_output, &stderr_style, raw_err.data(),
                 raw_err.size());

  if (opts.tagged_stream && splitter.bytes() > 0 && !splitter.saw_tag())
    out.warning =
        "the program produced unframed output; it was probably built without "
        "the injected header (rebuild with --force)";

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
