#include "app/interactive_ui.hpp"

#include "app/console_bar.hpp"
#include "app/line_edit.hpp"
#include "app/status_line.hpp"
#include "base/terminal.hpp"
#include "monitor/monitor.hpp"

#include <windows.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstring>
#include <deque>
#include <iostream>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace coding {
namespace interactive {

namespace {

constexpr std::size_t kMaxPending = 64u * 1024u * 1024u;

constexpr int kMaxKeysPerPass = 4096;

struct Shared {
  std::mutex m;
  std::condition_variable cv;

  std::string pending;
  std::size_t dropped{0};

  std::deque<std::string> lines;
  std::string current;
  std::size_t current_at{0};
  bool eof{false};

  std::atomic<bool> stop{false};
  std::atomic<bool> done{false};

  void *process{nullptr};
  void *job{nullptr};
  bool started{false};
  std::chrono::steady_clock::time_point start{};
};

} // namespace

bool run(const AppConfig &cfg, const SingleRunOption &base,
         SingleRunResult &out) {
  term::Session session;
  std::string error;
  if (!term::Session::open(session, error, term::Screen::Normal))
    return false;

  bar::Bar bar;
  if (!bar.open(session, error))
    return false;

  const unsigned tick = cfg.status_interval_ms == 0
                            ? 100u
                            : static_cast<unsigned>(cfg.status_interval_ms);

  Shared shared;
  SingleRunOption opt = base;
  opt.stdin_from_console = false;

  opt.on_started = [&shared](void *process, void *job) {
    shared.process = process;
    shared.job = job;
    shared.start = std::chrono::steady_clock::now();
    shared.started = true;
  };

  opt.on_output = [&shared](const char *data, std::size_t n, bool) {
    std::lock_guard<std::mutex> lock(shared.m);
    if (shared.pending.size() + n > kMaxPending) {
      shared.dropped += n;
      return;
    }
    shared.pending.append(data, n);
  };

  opt.stdin_source = [&shared](char *buf, std::size_t n) -> std::size_t {
    std::unique_lock<std::mutex> lock(shared.m);
    for (;;) {
      if (shared.current_at < shared.current.size()) {
        const std::size_t take =
            std::min(n, shared.current.size() - shared.current_at);
        std::memcpy(buf, shared.current.data() + shared.current_at, take);
        shared.current_at += take;
        if (shared.current_at == shared.current.size()) {
          shared.current.clear();
          shared.current_at = 0;
        }
        return take;
      }
      if (shared.eof || shared.stop.load(std::memory_order_relaxed))
        return 0;
      shared.cv.wait(lock, [&shared] {
        return !shared.lines.empty() || shared.eof ||
               shared.stop.load(std::memory_order_relaxed);
      });
      if (!shared.lines.empty()) {
        shared.current = std::move(shared.lines.front());
        shared.lines.pop_front();
        shared.current_at = 0;
      }
    }
  };

  SingleRunResult result;
  std::thread worker([&]() {
    result = run_single(opt);
    shared.done.store(true, std::memory_order_relaxed);
  });

  line::Editor editor;
  status::Line spinner;
  monitor::Watch watch;
  bool interrupted = false;

  while (!shared.done.load(std::memory_order_relaxed)) {

    term::Key key = term::Key::Text;
    char32_t ch = 0;
    bool got = session.read_any(key, ch, tick);
    for (int i = 0; got && i < kMaxKeysPerPass; ++i) {
      if (key == term::Key::Resize) {
        bar.resize();
      } else {
        const line::Outcome action = editor.feed(key, ch);
        if (action == line::Outcome::Submitted) {
          {
            std::lock_guard<std::mutex> lock(shared.m);
            shared.lines.push_back(editor.buffer + "\n");
          }

          bar.commit_line();
          editor.clear();
          bar.set_input(editor.text(), editor.cursor_columns());
          bar.paint();
          shared.cv.notify_all();
        } else if (action == line::Outcome::Interrupt && !interrupted) {
          interrupted = true;
          {
            std::lock_guard<std::mutex> lock(shared.m);
            shared.eof = true;
          }
          shared.cv.notify_all();
          shared.stop.store(true, std::memory_order_relaxed);
          if (shared.job != nullptr)
            TerminateJobObject(static_cast<HANDLE>(shared.job), 1);
        }
      }
      got = session.read_any(key, ch, 0);
    }

    if (shared.started) {
      const monitor::Sample now =
          monitor::sample(shared.process, shared.job, shared.start);
      spinner.advance(watch.cpu_advanced(now));
      watch.update(now);
      const term::Size size = session.size();
      bar.set_status(spinner.render(now.cpu_ms(),
                                    size.columns > 1 ? size.columns - 1 : 1));
    }

    std::string chunk;
    {
      std::lock_guard<std::mutex> lock(shared.m);
      chunk.swap(shared.pending);
    }
    if (!chunk.empty())
      bar.write_output(chunk);
    bar.set_input(editor.text(), editor.cursor_columns());
    bar.paint();
  }

  worker.join();

  std::string tail;
  {
    std::lock_guard<std::mutex> lock(shared.m);
    tail.swap(shared.pending);
  }
  if (!tail.empty())
    bar.write_output(tail);

  bar.close();

  if (shared.dropped != 0)
    std::cerr << "[run] " << shared.dropped
              << " bytes of output were dropped: the console could not keep "
                 "up\n";
  if (interrupted)
    std::cerr << "[run] interrupted\n";

  out = std::move(result);
  return true;
}

} // namespace interactive
} // namespace coding
