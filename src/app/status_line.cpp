#include "app/status_line.hpp"

#include "base/text.hpp"

namespace coding {
namespace status {

bool Line::advance(bool cpu_active) noexcept {
  if (!cpu_active)
    return false;
  frame = (frame + 1) % kFrames.size();
  return true;
}

std::string Line::frame_text() const {
  return std::string(1, kFrames[frame % kFrames.size()]);
}

std::string Line::render(std::chrono::milliseconds cpu,
                         std::size_t width) const {

  std::string text =
      "[" + frame_text() + "] cpu " + std::to_string(cpu.count()) + " ms";

  if (width == 0 || text::display_width(text) <= width)
    return text;

  std::string cut;
  std::size_t i = 0;
  while (i < text.size()) {
    const text::CharWidth w = text::measure(text, i);
    if (w.bytes == 0 || text::display_width(cut) + w.columns > width)
      break;
    cut.append(text, i, w.bytes);
    i += w.bytes;
  }
  return cut;
}

} // namespace status
} // namespace coding
