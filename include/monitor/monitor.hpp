#ifndef MONITOR_HPP
#define MONITOR_HPP

#include <chrono>
#include <cstddef>

namespace coding {

namespace monitor {

struct Sample {

  std::chrono::nanoseconds cpu{0};

  std::chrono::milliseconds wall{0};
  std::size_t peak_bytes{0};
  std::size_t current_bytes{0};
  bool alive{true};
  unsigned long exit_code{0};

  std::chrono::milliseconds cpu_ms() const {
    return std::chrono::duration_cast<std::chrono::milliseconds>(cpu);
  }
};

Sample sample(void *process, void *job,
              std::chrono::steady_clock::time_point started);

struct Watch {
  Sample prev;
  bool has_prev{false};

  void reset() noexcept {
    prev = Sample{};
    has_prev = false;
  }

  void update(const Sample &now) noexcept {
    prev = now;
    has_prev = true;
  }

  bool cpu_advanced(const Sample &now) const noexcept {
    return has_prev && now.cpu > prev.cpu;
  }
};

} // namespace monitor
} // namespace coding

#endif // MONITOR_HPP
