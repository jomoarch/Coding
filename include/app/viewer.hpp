#ifndef VIEWER_HPP
#define VIEWER_HPP

#include "base/terminal.hpp"
#include "compare/compare.hpp"

#include <cstddef>
#include <string>
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
                   std::size_t height);

int view(const CompareResult &result);

} // namespace viewer

#endif // VIEWER_HPP
