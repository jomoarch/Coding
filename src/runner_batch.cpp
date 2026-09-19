#include "runner_batch.hpp"

#include <algorithm>
#include <atomic>
#include <thread>

std::vector<ResultUnit> run_all(const BatchOptions &opts) {
  const auto &pairs = opts.pairs;
  const std::size_t n = pairs.size();

  std::vector<ResultUnit> r(n);
  if (n == 0)
    return r;

  const unsigned hw = std::max(1u, std::thread::hardware_concurrency());
  const std::size_t concurrency =
      std::min<std::size_t>({n, hw, opts.thread_max});

  std::atomic<std::size_t> next{0};
  std::vector<std::thread> workers;
  workers.reserve(concurrency);

  auto worker_fn = [&]() {
    while (true) {
      std::size_t i = next.fetch_add(1, std::memory_order_relaxed);
      if (i >= n)
        break;

      const auto &p = pairs[i];
      r[i].name = p.name;

      try {
        RunnerOptions ro;
        ro.exe_path = opts.exe_path;
        ro.work_dir = opts.work_dir / ("t" + std::to_string(i));
        ro.input_path = p.input_path;
        ro.output_path = p.output_path;
        ro.time_limit = opts.time_limit;
        ro.memory_limit_bytes = opts.memory_limit_bytes;

        std::error_code ec;
        std::filesystem::create_directories(ro.work_dir, ec);
        if (ec) {
          r[i].result.status = RunnerStatus::SystemError;
          r[i].result.message = "Failed to create work_dir: " + ec.message();
          continue;
        }
        r[i].result = run_exe(ro);
      } catch (const std::exception &e) {
        r[i].result.status = RunnerStatus::SystemError;
        r[i].result.message = std::string("Worker exception: ") + e.what();
      }
    }
  };

  for (std::size_t t = 0; t < concurrency; ++t) {
    workers.emplace_back(worker_fn);
  }
  for (auto &w : workers)
    w.join();

  return r;
}