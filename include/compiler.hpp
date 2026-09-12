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
};

CompilerResult compile_source(const CompilerOptions &opts);

#endif // COMPILER_HPP