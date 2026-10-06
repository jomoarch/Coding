#ifndef COMPILER_HPP
#define COMPILER_HPP

#include "base/result.hpp"

#include <filesystem>
#include <string>
#include <vector>
namespace coding {

struct CompilerOptions {
  std::filesystem::path source_path;
  std::filesystem::path output_path;
  std::vector<std::string> args;

  std::filesystem::path inject_header;
};

struct CompilerResult : ResultBase {};

CompilerResult compile_source(const CompilerOptions &opts);

} // namespace coding

#endif // COMPILER_HPP