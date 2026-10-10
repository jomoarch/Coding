#ifndef STATUS_LINE_HPP
#define STATUS_LINE_HPP

#include <chrono>
#include <cstddef>
#include <string>
#include <string_view>

namespace coding {

namespace status {

inline constexpr std::string_view kFrames = "|/-\\";

struct Line {
  std::size_t frame{0};

  bool advance(bool cpu_active) noexcept;

  std::string render(std::chrono::milliseconds cpu, std::size_t width) const;

  std::string frame_text() const;
};

} // namespace status
} // namespace coding

#endif // STATUS_LINE_HPP
