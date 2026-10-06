#include "compare/case.hpp"

#include <string>
namespace coding {

const char *case_state_token(CaseState state) noexcept {
  switch (state) {
  case CaseState::Identical:
    return "matched";
  case CaseState::Differ:
    return "differ";
  case CaseState::NoOutput:
    return "no_output";
  case CaseState::NoAnswer:
    return "no_answer";
  case CaseState::Unreadable:
    return "unreadable";
  case CaseState::Failed:
    return "failed";
  }
  return "failed";
}

bool case_state_from_token(const std::string &token, CaseState &out) noexcept {
  if (token == "matched")
    out = CaseState::Identical;
  else if (token == "differ")
    out = CaseState::Differ;
  else if (token == "no_output")
    out = CaseState::NoOutput;
  else if (token == "no_answer")
    out = CaseState::NoAnswer;
  else if (token == "unreadable")
    out = CaseState::Unreadable;
  else if (token == "failed")
    out = CaseState::Failed;
  else
    return false;
  return true;
}
} // namespace coding
