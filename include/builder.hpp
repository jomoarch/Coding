#ifndef BUILDER_HPP
#define BUILDER_HPP

#include <filesystem>
#include <vector>
#include <string>

struct BuildOption {
  std::filesystem::path source_path;
  std::filesystem::path output_path;
  std::vector<std::string> args;

  std::vector<std::filesystem::path> extra_deps;

  bool force_rebuild{false};
};

struct BuildResult {
  bool success{false};
  bool rebuilt{false};
  std::string message;
};

BuildResult ensure_built(const BuildOption &opts);

#endif // BUILDER_HPP