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
#include <mutex>
#include <atomic>
namespace coding {

namespace {

inline constexpr char kFramedStdout = '\x01';
inline constexpr char kFramedStderr = '\x02';

std::mutex g_console_mutex;

void write_styled(bool colorize, const color::Style *style, const char *data,
                  std::size_t n) {
  if (n == 0)
    return;

  std::string buf;
  if (colorize && style)
    buf = (*style)(std::string_view(data, n));
  else
    buf.assign(data, n);

  std::lock_guard<std::mutex> lock(g_console_mutex);
  std::cout.flush();

  HANDLE h = GetStdHandle(STD_OUTPUT_HANDLE);
  DWORD written = 0;
  if (h && h != INVALID_HANDLE_VALUE &&
      ::WriteFile(h, buf.data(), static_cast<DWORD>(buf.size()), &written,
                  nullptr)) {
    return;
  }
  std::cout.write(buf.data(), static_cast<std::streamsize>(buf.size()));
  std::cout.flush();
}

struct Sink {
  bool echo{true};
  bool colorize{false};
  const color::Style *out_style{nullptr};
  const color::Style *err_style{nullptr};
  std::function<void(const char *, std::size_t, bool)> on_output;

  void write(const char *data, std::size_t n, bool is_stderr) const {
    if (on_output) {
      on_output(data, n, is_stderr);
      return;
    }
    if (!echo)
      return;
    write_styled(colorize, is_stderr ? err_style : out_style, data, n);
  }
};

void pump_both(HANDLE hOut, HANDLE hErr, const Sink &sink,
               std::string *out_sink, bool merge_err) {
  HANDLE handles[2] = {hOut, hErr};
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

    if (out_sink && (i == 0 || merge_err))
      out_sink->append(buf.data(), n);

    sink.write(buf.data(), n, i == 1);
  }
}

class TagSplitter {
public:
  TagSplitter(const Sink &sink, std::string *out_sink, bool merge_err)
      : sink_(sink), out_sink_(out_sink), merge_err_(merge_err) {}

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
      if (merge_err_ && out_sink_)
        out_sink_->append(data, n);
      sink_.write(data, n, true);
      return;
    }

    if (out_sink_)
      out_sink_->append(data, n);
    sink_.write(data, n, false);
  }

  const Sink &sink_;
  std::string *out_sink_;
  bool merge_err_;
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

  HANDLE hConsoleIn = INVALID_HANDLE_VALUE;
  std::thread stdin_forwarder;
  std::atomic<bool> stdin_stop{false};

  DWORD saved_console_mode = 0;
  bool console_mode_saved = false;

  if (opts.stdin_from_console) {
    hConsoleIn = GetStdHandle(STD_INPUT_HANDLE);
    if (hConsoleIn == nullptr || hConsoleIn == INVALID_HANDLE_VALUE) {
      out.result.message = "No console stdin available";
      return out;
    }
    DWORD mode = 0;
    stdin_is_console = GetConsoleMode(hConsoleIn, &mode) != FALSE;

    if (stdin_is_console) {
      saved_console_mode = mode;
      console_mode_saved =
          SetConsoleMode(hConsoleIn, mode | ENABLE_ECHO_INPUT) != FALSE;

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
    } else {
      hChildStdin = hConsoleIn;
      SetHandleInformation(hChildStdin, HANDLE_FLAG_INHERIT,
                           HANDLE_FLAG_INHERIT);
    }
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
  DWORD create_flags = CREATE_SUSPENDED | CREATE_NO_WINDOW;

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

  Sink sink;
  sink.echo = opts.echo;
  sink.colorize = opts.colorize_output;
  sink.out_style = &stdout_style;
  sink.err_style = &stderr_style;
  sink.on_output = opts.on_output;

  if (opts.on_started)
    opts.on_started(pi.hProcess, hJob.get());

  std::string captured;
  std::string raw_err;
  TagSplitter splitter(sink, &captured, opts.merge_stderr);

  std::thread pump;
  if (opts.tagged_stream)
    pump = std::thread(pump_tagged, hOutRead.get(), hErrRead.get(),
                       std::ref(splitter), &raw_err);
  else
    pump = std::thread(pump_both, hOutRead.get(), hErrRead.get(),
                       std::cref(sink), &captured, opts.merge_stderr);

  if (opts.stdin_source && hStdinWrite.valid()) {

    stdin_forwarder = std::thread([&]() {
      std::vector<char> buf(4096);
      while (!stdin_stop.load(std::memory_order_relaxed)) {
        const std::size_t want = opts.stdin_source(buf.data(), buf.size());
        if (want == 0)
          break;
        std::size_t done = 0;
        while (done < want) {
          DWORD wrote = 0;
          if (!WriteFile(hStdinWrite.get(), buf.data() + done,
                         static_cast<DWORD>(want - done), &wrote, nullptr) ||
              wrote == 0)
            break;
          done += wrote;
        }
        if (done < want)
          break;
      }
      hStdinWrite.reset();
    });
  } else if (opts.stdin_from_console && stdin_is_console &&
             hStdinWrite.valid()) {
    stdin_forwarder = std::thread([&]() {
      std::vector<char> fbuf(4096);
      std::vector<char> batch;

      constexpr DWORD kIdleMs = 15;
      constexpr std::size_t kMaxBatch = 256 * 1024;

      bool closed = false;
      while (!closed && !stdin_stop.load(std::memory_order_relaxed)) {
        DWORD r = ::WaitForSingleObject(hConsoleIn, 50);
        if (r == WAIT_TIMEOUT)
          continue;
        if (r != WAIT_OBJECT_0)
          break;

        batch.clear();
        for (;;) {
          DWORD n = 0;
          if (!::ReadFile(hConsoleIn, fbuf.data(),
                          static_cast<DWORD>(fbuf.size()), &n, nullptr) ||
              n == 0) {
            closed = true;
            break;
          }
          batch.insert(batch.end(), fbuf.begin(), fbuf.begin() + n);

          if (batch.size() >= kMaxBatch)
            break;
          if (::WaitForSingleObject(hConsoleIn, kIdleMs) != WAIT_OBJECT_0)
            break;
        }
        if (batch.empty())
          continue;

        DWORD written = 0;
        if (!::WriteFile(hStdinWrite.get(), batch.data(),
                         static_cast<DWORD>(batch.size()), &written, nullptr) ||
            written == 0)
          break;
      }
      hStdinWrite.reset();
    });
  } else if (hStdinWrite.valid()) {
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

  stdin_stop.store(true, std::memory_order_relaxed);
  if (stdin_forwarder.joinable())
    stdin_forwarder.join();

  pump.join();

  if (console_mode_saved && hConsoleIn != INVALID_HANDLE_VALUE)
    SetConsoleMode(hConsoleIn, saved_console_mode);

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
} // namespace coding
