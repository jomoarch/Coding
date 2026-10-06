#ifndef LIST_VIEW_HPP
#define LIST_VIEW_HPP

#include "base/terminal.hpp"

#include <cstddef>
#include <string>
#include <vector>
namespace coding {

namespace list_view {

struct Column {
  std::string title;
  bool right{false};
};

struct Row {
  std::vector<std::string> cells;
  bool marked{false};
};

struct State {
  std::size_t width{0};
  std::size_t header{2};
  std::size_t footer{1};
  std::size_t cursor{0};
  std::size_t top{0};

  std::string status;
  std::string message;
  std::string input;
  bool typing{false};
};

struct Reading {
  term::Key key{term::Key::Text};
  char32_t text{0};
};

enum class Event { None, Moved, Quit, Open, Submit, Key };

Event apply(State &state, const Reading &reading, std::size_t height,
            std::size_t rows);

std::string render(const std::vector<Row> &rows, const State &state,
                   std::size_t height, const std::vector<Column> &columns);

void reshape(State &state, std::size_t width, std::size_t height,
             std::size_t rows);

} // namespace list_view

} // namespace coding

#endif // LIST_VIEW_HPP
