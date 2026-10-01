#ifndef COMPARE_CASE_HPP
#define COMPARE_CASE_HPP

#include "compare/compare.hpp"

#include <string>

enum class CaseState {
  Identical,
  Differ,
  NoOutput,
  NoAnswer,
  Unreadable,
  Failed
};

struct Case {
  std::string name;
  CaseState state{CaseState::Failed};
  CompareResult result;
  std::string note;
};

const char *case_state_token(CaseState state) noexcept;
bool case_state_from_token(const std::string &token, CaseState &out) noexcept;

#endif // COMPARE_CASE_HPP
