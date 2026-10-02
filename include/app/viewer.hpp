#ifndef VIEWER_HPP
#define VIEWER_HPP

#include "base/terminal.hpp"
#include "compare/case.hpp"
#include "compare/compare.hpp"

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace viewer {

struct Node {
  const LineDiff *diff{nullptr};

  bool token_case{false};
  char marker{'!'};

  bool expanded{false};
  std::size_t token{0}; // 0-based
  std::vector<std::string> detail;
};

struct State {
  std::vector<Node> nodes;
  std::size_t width{0};
  std::size_t header{1};
  std::size_t cursor{0};
  std::size_t top{0};
};

std::size_t node_height(const Node &node) noexcept;

State make_state(const CompareResult &result, std::size_t width);

void reshape(State &state, std::size_t width, std::size_t height);

void apply(State &state, term::Key key, std::size_t height);

std::string render(const CompareResult &result, const State &state,
                   std::size_t height, std::string_view title = {});

int view(const CompareResult &result);

struct BatchState {
  std::size_t width{0};
  std::size_t header{1};
  std::size_t cursor{0};
  std::size_t top{0};
  std::ptrdiff_t entered{-1};
  State inner;
};

bool case_browsable(const Case &item) noexcept;

enum class BatchAction { Feed, Quit, Back };

[[nodiscard]] BatchAction batch_action(const BatchState &state,
                                       term::Key key) noexcept;

BatchState make_batch_state(std::size_t width);

void reshape_batch(BatchState &state, const std::vector<Case> &cases,
                   std::size_t width, std::size_t height);

void apply_batch(BatchState &state, const std::vector<Case> &cases,
                 term::Key key, std::size_t height);

std::string render_batch(const std::vector<Case> &cases,
                         const BatchState &state, std::size_t height);

int view_batch(const std::vector<Case> &cases);

} // namespace viewer

#endif // VIEWER_HPP
