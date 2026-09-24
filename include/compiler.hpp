#ifndef COMPILER_HPP
#define COMPILER_HPP

#include <filesystem>
#include <string>
#include <vector>

struct CompilerOptions {
  std::filesystem::path source_path;
  std::filesystem::path output_path;
  std::vector<std::string> args;
};

struct CompilerResult {
  bool success{false};
  std::string message;

    bool ok() const noexcept { return success; }
  explicit operator bool() const noexcept { return success; }
};

CompilerResult compile_source(const CompilerOptions &opts);

#endif // COMPILER_HPP