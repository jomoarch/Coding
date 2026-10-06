#include "app/list_view.hpp"

#include "base/text.hpp"

#include <algorithm>
#include <string>
#include <string_view>
#include <vector>
namespace coding {

namespace list_view {

namespace {

std::size_t fit_width(std::size_t width) noexcept {
  return width < 1 ? 1 : width;
}

std::size_t list_rows(std::size_t height, std::size_t header) noexcept {
  return height > header ? height - header : 0;
}

std::string clip(std::string_view text, std::size_t width) {
  std::string out;
  std::size_t columns = 0;
  for (std::size_t i = 0; i < text.size();) {
    const text::CharWidth unit = text::measure(text, i);
    if (unit.bytes == 0 || columns + unit.columns > width)
      break;
    out.append(text.substr(i, unit.bytes));
    columns += unit.columns;
    i += unit.bytes;
  }
  return out;
}

void put_row(std::string &frame, const std::string &content, bool highlight,
             std::size_t width, std::size_t height, std::size_t &used) {
  if (used >= height)
    return;

  const std::string row = clip(content, width);
  if (highlight) {
    const std::size_t columns = text::display_width(row);
    frame += "\x1b[7m" + row;
    if (columns < width)
      frame += std::string(width - columns, ' ');
    frame += "\x1b[0m";
  } else {
    frame += row + "\x1b[K";
  }

  ++used;
  if (used < height)
    frame += "\r\n";
}

void clamp(State &state, std::size_t height, std::size_t rows, bool follow) {
  const std::size_t visible = list_rows(height, state.header + state.footer);
  const std::size_t max_top = rows > visible ? rows - visible : 0;

  if (follow && rows != 0 && visible != 0) {
    if (state.cursor < state.top)
      state.top = state.cursor;
    else if (state.cursor >= state.top + visible)
      state.top = state.cursor - visible + 1;
  }
  state.top = std::min(state.top, max_top);
  if (rows == 0)
    state.top = 0;
}

void append_utf8(std::string &out, char32_t codepoint) {
  if (codepoint < 0x80) {
    out.push_back(static_cast<char>(codepoint));
  } else if (codepoint < 0x800) {
    out.push_back(static_cast<char>(0xC0 | (codepoint >> 6)));
    out.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
  } else if (codepoint < 0x10000) {
    out.push_back(static_cast<char>(0xE0 | (codepoint >> 12)));
    out.push_back(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F)));
    out.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
  } else {
    out.push_back(static_cast<char>(0xF0 | (codepoint >> 18)));
    out.push_back(static_cast<char>(0x80 | ((codepoint >> 12) & 0x3F)));
    out.push_back(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F)));
    out.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
  }
}

} // namespace

Event apply(State &state, const Reading &reading, std::size_t height,
            std::size_t rows) {
  if (state.typing) {
    switch (reading.key) {
    case term::Key::Enter:
      return Event::Submit;
    case term::Key::Quit:
      state.input.clear();
      state.typing = false;
      return Event::None;
    case term::Key::Backspace:
      if (!state.input.empty()) {
        std::size_t at = state.input.size() - 1;
        while (at > 0 &&
               (static_cast<unsigned char>(state.input[at]) & 0xC0) == 0x80)
          --at;
        state.input.erase(at);
      }
      return Event::None;
    case term::Key::Text:
      append_utf8(state.input, reading.text);
      return Event::None;
    default:
      return Event::None;
    }
  }

  switch (reading.key) {
  case term::Key::Text:
    if (reading.text == U'j') {
      if (state.cursor + 1 < rows)
        ++state.cursor;
    } else if (reading.text == U'k') {
      if (state.cursor > 0)
        --state.cursor;
    } else if (reading.text == U'q') {
      return Event::Quit;
    } else if (reading.text == U':') {
      state.typing = true;
      state.input.clear();
      return Event::None;
    } else {
      return Event::Key;
    }
    break;

  case term::Key::Up:
    if (state.cursor > 0)
      --state.cursor;
    break;
  case term::Key::Down:
    if (state.cursor + 1 < rows)
      ++state.cursor;
    break;
  case term::Key::ViewUp:
    if (state.top > 0)
      --state.top;
    clamp(state, height, rows, false);
    return Event::Moved;
  case term::Key::ViewDown:
    ++state.top;
    clamp(state, height, rows, false);
    return Event::Moved;
  case term::Key::Enter:
    return Event::Open;
  case term::Key::Quit:
    return Event::Quit;
  case term::Key::Resize:
    return Event::None;
  default:
    return Event::Key;
  }

  clamp(state, height, rows, true);
  return Event::Moved;
}

std::string render(const std::vector<Row> &rows, const State &state,
                   std::size_t height, const std::vector<Column> &columns) {
  std::string frame = "\x1b[H";
  std::size_t used = 0;
  const std::size_t width = fit_width(state.width);

  std::string status = state.status;
  if (!state.message.empty())
    status += (status.empty() ? "" : "   ") + state.message;
  put_row(frame, status, false, width, height, used);

  std::vector<std::size_t> widths(columns.size(), 0);
  for (std::size_t c = 0; c < columns.size(); ++c) {
    widths[c] = text::display_width(columns[c].title);
    for (const Row &row : rows) {
      if (c < row.cells.size())
        widths[c] = std::max(widths[c], text::display_width(row.cells[c]));
    }
  }

  const auto build = [&](const std::vector<std::string> &cells,
                         bool marked) -> std::string {
    std::string line = marked ? "* " : "  ";
    for (std::size_t c = 0; c < columns.size(); ++c) {
      const std::string cell = c < cells.size() ? cells[c] : std::string();
      if (c != 0)
        line += "  ";
      line += columns[c].right ? text::pad_left(cell, widths[c])
                               : text::pad_right(cell, widths[c]);
    }
    return line;
  };

  std::vector<std::string> titles;
  titles.reserve(columns.size());
  for (const Column &column : columns)
    titles.push_back(column.title);
  put_row(frame, build(titles, false), false, width, height, used);

  const std::size_t visible = list_rows(height, state.header + state.footer);
  std::size_t emitted = 0;
  for (std::size_t i = state.top; i < rows.size() && emitted < visible;
       ++i, ++emitted) {
    const bool cursor_here = i == state.cursor;
    put_row(frame, build(rows[i].cells, rows[i].marked), cursor_here, width,
            height, used);
  }

  while (used + 1 < height)
    put_row(frame, std::string(), false, width, height, used);

  if (used < height) {
    std::string line = ": " + state.input;
    if (state.typing)
      line += "_";
    else if (state.input.empty())
      line += "(press : for commands, ? for help)";
    put_row(frame, line, false, width, height, used);
  }

  return frame;
}

void reshape(State &state, std::size_t width, std::size_t height,
             std::size_t rows) {
  state.width = fit_width(width);
  state.header = 2;
  state.footer = 1;
  clamp(state, height, rows, true);
}

} // namespace list_view
} // namespace coding
