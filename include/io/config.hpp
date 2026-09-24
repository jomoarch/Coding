#ifndef CONFIG_HPP
#define CONFIG_HPP

#include "base\result.hpp"

#include <chrono>
#include <filesystem>
#include <string>
#include <vector>
#include <cstddef>

struct AppConfig {
  std::filesystem::path config_path;

  // compiler
  std::filesystem::path source_path;
  std::filesystem::path exe_path;
  std::vector<std::string> args;

  // runner
  std::filesystem::path work_dir;
  std::chrono::milliseconds time_limit{0};
  std::size_t memory_limit_bytes{0};

  // io
  std::filesystem::path input_dir;
  std::filesystem::path output_dir;
  std::filesystem::path single_input;
  std::filesystem::path single_output;
  bool colorize_output{true};

  // thread
  std::size_t thread_max{1};

  // build
  bool force_rebuild{false};
};

struct ConfigResult : ResultBase {
  AppConfig config;
};

[[nodiscard]] ConfigResult load_config(const std::filesystem::path &path);

#endif // CONFIG_HPP