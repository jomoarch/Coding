#ifndef COMPARE_SERIALIZE_HPP
#define COMPARE_SERIALIZE_HPP

#include "base/result.hpp"
#include "compare/compare.hpp"

#include <filesystem>

struct SaveResult : ResultBase {};

struct LoadResult : ResultBase {
  CompareResult result;
};

[[nodiscard]] SaveResult save_compare_result(const CompareResult &result,
                                             const std::filesystem::path &path);

[[nodiscard]] LoadResult load_compare_result(const std::filesystem::path &path);

#endif // COMPARE_SERIALIZE_HPP
