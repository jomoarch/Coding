#ifndef LINE_EDIT_HPP
#define LINE_EDIT_HPP

#include "base/terminal.hpp"

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace coding {

namespace line {

enum class Outcome { None, Edited, Submitted, Interrupt };

struct Editor {
  std::string buffer;
  std::size_t cursor{0};

  Outcome feed(term::Key key, char32_t ch);

  void insert_text(std::string_view text);

  void clear() noexcept {
    buffer.clear();
    cursor = 0;
  }

  std::size_t cursor_columns() const;

  std::string_view text() const noexcept { return buffer; }

private:
  void insert(char32_t ch);
  void backspace();
  void move_left();
  void move_right();
};

std::string encode_utf8(char32_t ch);

struct Paste {
  std::vector<std::string> lines;
  std::string tail;
};

Paste split_paste(std::string_view text);

} // namespace line
} // namespace coding

#endif // LINE_EDIT_HPP
