#ifndef COMPILER_HPP
#define COMPILER_HPP

#include "base/result.hpp"

#include <filesystem>
#include <string>
#include <vector>

struct CompilerOptions {
  std::filesystem::path source_path;
  std::filesystem::path output_path;
  std::vector<std::string> args;
};

struct CompilerResult : ResultBase {};

CompilerResult compile_source(const CompilerOptions &opts);

#endif // COMPILER_HPP