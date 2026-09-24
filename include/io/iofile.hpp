#ifndef IOFILE_HPP
#define IOFILE_HPP

#include "base\result.hpp"

#include <filesystem>
#include <string>
#include <vector>

struct IOFileOption {
  std::filesystem::path input_dir;
  std::filesystem::path output_dir;
};

struct FilePair {
  std::string name;
  std::filesystem::path input_path;
  std::filesystem::path output_path;
};

struct IOFileResult : ResultBase {
  std::vector<FilePair> pairs;
};

IOFileResult gen_filepair(const IOFileOption &opts);

#endif // IOFILE_HPP