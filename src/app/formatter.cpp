#include "app/formatter.hpp"
#include "base/color.hpp"
#include "base/text.hpp"

#include <iomanip>
#include <sstream>
#include <algorithm>
#include <utility>

std::string format_memory(std::size_t bytes) {
  if (bytes == 0)
    return "0.00  B";

  static const char *units[] = {"  B", " KB", " MB", " GB"};
  constexpr int last_unit = 3;

  double v = static_cast<double>(bytes);
  int i = 0;
  while (v >= 1023.995 && i < last_unit) {
    v /= 1024.0;
    ++i;
  }

  std::ostringstream oss;
  oss << std::fixed << std::setprecision(2) << v << units[i];
  return oss.str();
}

std::vector<std::string> format_results(const std::vector<ResultUnit> &results,
                                        bool show_message, bool show_wall_time,
                                        std::ostream &os) {
  std::vector<std::string> out;
  out.reserve(results.size());
  if (results.empty())
    return out;

  struct Row {
    std::string name;
    std::string wall;
    std::string cpu;
    std::string mem;
    std::string msg;
    RunnerStatus status;
  };

  std::vector<Row> rows;
  rows.reserve(results.size());

  std::size_t w_name = 0, w_wall = 0, w_cpu = 0, w_mem = 0;

  for (const auto &u : results) {
    Row row;
    row.name = u.name;

    row.wall = std::to_string(u.result.wall_time.count()) + " ms";
    row.cpu = std::to_string(u.result.cpu_time.count()) + " ms";

    row.mem = format_memory(u.result.memory_bytes);

    row.msg = u.result.message;

    row.status = u.result.status;

    w_name = std::max(w_name, text::display_width(row.name));
    w_wall = std::max(w_wall, text::display_width(row.wall));
    w_cpu = std::max(w_cpu, text::display_width(row.cpu));
    w_mem = std::max(w_mem, text::display_width(row.mem));

    rows.push_back(std::move(row));
  }

  for (const auto &row : rows) {
    std::string o = text::pad_right(row.name, w_name);
    o += "  ";

    if (show_wall_time) {
      o += text::pad_left(row.wall, w_wall);
      o += "  ";
    }
    o += text::pad_left(row.cpu, w_cpu);
    o += "  ";
    o += text::pad_left(row.mem, w_mem);
    o += "  ";
    if (show_message)
      o += row.msg;

    switch (row.status) {
    case RunnerStatus::Success: {
      o = color::paint(os, o, {color::Code::Bold, color::Code::Green});
      break;
    }
    case RunnerStatus::TimeLimitExceeded:
    case RunnerStatus::MemoryLimitExceeded: {
      o = color::paint(os, o, {color::Code::BgWhite, color::Code::Black});
      break;
    }
    case RunnerStatus::RuntimeError: {
      o = color::paint(os, o, {color::Code::Bold, color::Code::Magenta});
      break;
    }
    case RunnerStatus::SystemError: {
      o = color::paint(os, o, {color::Code::Bold, color::Code::BrightMagenta});
      break;
    }
    default:
      break;
    }

    out.push_back(std::move(o));
  }
  return out;
}