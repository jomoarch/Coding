#include "app/line_edit.hpp"

#include "base/text.hpp"

namespace coding {
namespace line {

namespace {

bool is_continuation(char c) {
  return (static_cast<unsigned char>(c) & 0xC0) == 0x80;
}

std::size_t step_back(std::string_view s, std::size_t from) {
  if (from == 0)
    return 0;
  std::size_t i = from - 1;
  while (i > 0 && is_continuation(s[i]))
    --i;
  return i;
}

std::size_t step_forward(std::string_view s, std::size_t from) {
  if (from >= s.size())
    return s.size();
  std::size_t i = from + 1;
  while (i < s.size() && is_continuation(s[i]))
    ++i;
  return i;
}

} // namespace

std::string encode_utf8(char32_t ch) {
  std::string out;
  if (ch < 0x80) {
    out.push_back(static_cast<char>(ch));
  } else if (ch < 0x800) {
    out.push_back(static_cast<char>(0xC0 | (ch >> 6)));
    out.push_back(static_cast<char>(0x80 | (ch & 0x3F)));
  } else if (ch < 0x10000) {
    out.push_back(static_cast<char>(0xE0 | (ch >> 12)));
    out.push_back(static_cast<char>(0x80 | ((ch >> 6) & 0x3F)));
    out.push_back(static_cast<char>(0x80 | (ch & 0x3F)));
  } else {
    out.push_back(static_cast<char>(0xF0 | (ch >> 18)));
    out.push_back(static_cast<char>(0x80 | ((ch >> 12) & 0x3F)));
    out.push_back(static_cast<char>(0x80 | ((ch >> 6) & 0x3F)));
    out.push_back(static_cast<char>(0x80 | (ch & 0x3F)));
  }
  return out;
}

void Editor::insert(char32_t ch) {
  const std::string bytes = encode_utf8(ch);
  buffer.insert(cursor, bytes);
  cursor += bytes.size();
}

void Editor::backspace() {
  if (cursor == 0)
    return;
  const std::size_t start = step_back(buffer, cursor);
  buffer.erase(start, cursor - start);
  cursor = start;
}

void Editor::move_left() { cursor = step_back(buffer, cursor); }

void Editor::move_right() { cursor = step_forward(buffer, cursor); }

std::size_t Editor::cursor_columns() const {
  return text::display_width(std::string_view(buffer).substr(0, cursor));
}

Outcome Editor::feed(term::Key key, char32_t ch) {
  switch (key) {
  case term::Key::Text:
    if (ch >= 0x20 && ch != 0x7F) {
      insert(ch);
      return Outcome::Edited;
    }
    return Outcome::None;
  case term::Key::Backspace:
    backspace();
    return Outcome::Edited;
  case term::Key::Left:
    move_left();
    return Outcome::Edited;
  case term::Key::Right:
    move_right();
    return Outcome::Edited;
  case term::Key::Enter:
    return Outcome::Submitted;
  case term::Key::Quit:
    return Outcome::Interrupt;
  default:
    return Outcome::None;
  }
}

} // namespace line
} // namespace coding
