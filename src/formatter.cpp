#include "formatter.hpp"

#include <iomanip>
#include <sstream>
#include <algorithm>

std::string format_memory(std::size_t bytes) {
  if (bytes == 0)
    return "0.00  B";

  static const char *units[] = {"  B", " KB", " MB", " GB"};
  constexpr int last_unit = 3;

  double v = static_cast<double>(bytes);
  int i = 0;
  while (v >= 1023.955 && i < last_unit) {
    v /= 1024.0;
    ++i;
  }

  std::ostringstream oss;
  oss << std::fixed << std::setprecision(2) << v << units[i];
  return oss.str();
}

std::vector<std::string> format_results(const std::vector<ResultUnit> &results,
                                        bool show_message,
                                        bool show_wall_time) {
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

    w_name = std::max(w_name, row.name.size());
    w_wall = std::max(w_wall, row.wall.size());
    w_cpu = std::max(w_cpu, row.cpu.size());
    w_mem = std::max(w_mem, row.mem.size());

    rows.push_back(std::move(row));
  }

  for (const auto &row : rows) {
    std::ostringstream line;

    line << std::left << std::setw(static_cast<int>(w_name)) << row.name
         << "  ";

    if (show_wall_time) {
      line << std::setw(static_cast<int>(w_wall)) << std::right << row.wall
           << "  ";
    }
    line << std::setw(static_cast<int>(w_cpu)) << std::right << row.cpu << "  ";
    line << std::setw(static_cast<int>(w_mem)) << std::right << row.mem << "  ";
    if (show_message) {
      line << row.msg;
    }

    out.push_back(line.str());
  }
  return out;
}