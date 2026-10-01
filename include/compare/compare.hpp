#ifndef COMPARE_HPP
#define COMPARE_HPP

#include "base/result.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

enum class LineKind { Differ, OnlyOutput, OnlyExpect };

struct TokenDiff {
  std::size_t index{0}; // 0-based
  std::string output_token;
  std::string expect_token;
};

struct LineDiff {
  std::size_t line_no{0}; // 1-based
  LineKind kind{LineKind::Differ};
  std::string output_line;
  std::string expect_line;
  bool whitespace_only{false};
  std::vector<TokenDiff> token_diffs;
};

struct CompareOption {
  std::filesystem::path output_path;
  std::filesystem::path expect_path;
};

struct CompareResult : ResultBase {
  bool exact_match{false};

  std::size_t unmatched_line_count{0};
  std::size_t output_line_count{0};
  std::size_t expect_line_count{0};

  std::vector<LineDiff> unmatched_lines;

  std::uint64_t output_hash{0};
  std::uint64_t expect_hash{0};

  std::string warning;
};

[[nodiscard]] CompareResult compare_output(const CompareOption &opts);

#endif // COMPARE_HPP
