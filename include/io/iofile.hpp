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

struct ComparePair {
  std::string name;
  std::filesystem::path output_path;
  std::filesystem::path expect_path;
};

struct ComparePairResult : ResultBase {
  std::vector<ComparePair> pairs;
};

[[nodiscard]] ComparePairResult
gen_compare_pairs(const std::filesystem::path &input_dir,
                  const std::filesystem::path &output_dir,
                  const std::filesystem::path &answer_dir);

bool case_name_less(const std::string &a, const std::string &b);

#endif // IOFILE_HPP