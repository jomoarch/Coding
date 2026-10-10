#include "app/console_bar.hpp"

#include "base/text.hpp"

#include <windows.h>

#include <algorithm>
#include <string>

namespace coding {
namespace bar {

namespace {

constexpr std::size_t kMinRows = 4;
constexpr std::size_t kMinColumns = 12;

void raw_write(HANDLE h, std::string_view bytes) {
  std::size_t done = 0;
  while (done < bytes.size()) {
    const DWORD want = static_cast<DWORD>(
        std::min<std::size_t>(bytes.size() - done, 1u << 20));
    DWORD wrote = 0;
    if (!WriteFile(h, bytes.data() + done, want, &wrote, nullptr) || wrote == 0)
      return;
    done += wrote;
  }
}

} // namespace

Bar::~Bar() { close(); }

bool Bar::open(term::Session &session, std::string &error) {
  if (active_)
    return true;

  input_ = session.input_handle();
  output_ = session.output_handle();
  if (input_ == nullptr || output_ == nullptr) {
    error = "the session has no console handles";
    return false;
  }

  const term::Size size = session.size();
  if (size.rows < kMinRows || size.columns < kMinColumns) {
    error = "the terminal is too small for a status line";
    return false;
  }
  rows_ = size.rows;
  columns_ = size.columns;

  int top = 0;
  Pos at{};
  if (!cursor_now(at, top)) {
    error = "cannot read the cursor position";
    return false;
  }
  anchor_ = at;
  active_ = true;
  paint();
  return true;
}

void Bar::close() noexcept {
  if (!active_)
    return;
  active_ = false;
  erase_ours();
  place(anchor_);
}

void Bar::place(const Pos &at) const {
  CONSOLE_SCREEN_BUFFER_INFO info{};
  if (!GetConsoleScreenBufferInfo(static_cast<HANDLE>(output_), &info))
    return;
  const int row = info.srWindow.Top + at.row;
  if (row < 0 || row >= info.dwSize.Y)
    return;
  const int column =
      std::min<int>(std::max<int>(0, at.column), info.dwSize.X - 1);
  const COORD target{static_cast<SHORT>(info.srWindow.Left + column),
                     static_cast<SHORT>(row)};
  SetConsoleCursorPosition(static_cast<HANDLE>(output_), target);
}

void Bar::write_bytes(std::string_view bytes) const {
  raw_write(static_cast<HANDLE>(output_), bytes);
}

bool Bar::cursor_now(Pos &out, int &window_top) const {
  CONSOLE_SCREEN_BUFFER_INFO info{};
  if (!GetConsoleScreenBufferInfo(static_cast<HANDLE>(output_), &info))
    return false;
  out.row = info.dwCursorPosition.Y - info.srWindow.Top;
  out.column = info.dwCursorPosition.X - info.srWindow.Left;
  window_top = info.srWindow.Top;
  return true;
}

bool Bar::skip_for_selection() const {
  CONSOLE_SELECTION_INFO info{};
  if (!GetConsoleSelectionInfo(&info))
    return false;
  return (info.dwFlags & CONSOLE_SELECTION_IN_PROGRESS) != 0;
}

std::size_t Bar::echo_columns() const {
  return text::display_width(input_text_);
}

void Bar::erase_ours() {
  if (!active_)
    return;

  CONSOLE_SCREEN_BUFFER_INFO info{};
  if (!GetConsoleScreenBufferInfo(static_cast<HANDLE>(output_), &info))
    return;

  const int first_row = info.srWindow.Top + anchor_.row;
  const int first_column = info.srWindow.Left + anchor_.column;
  const SHORT width = info.dwSize.X;
  DWORD written = 0;
  for (int i = 0; i < 2; ++i) {
    const int row = first_row + i;
    if (row < 0 || row >= info.dwSize.Y)
      continue;
    const int from = i == 0 ? first_column : info.srWindow.Left;
    if (from >= width)
      continue;
    const COORD at{static_cast<SHORT>(from), static_cast<SHORT>(row)};
    FillConsoleOutputCharacterW(static_cast<HANDLE>(output_), L' ',
                                static_cast<DWORD>(width - from), at, &written);
    FillConsoleOutputAttribute(static_cast<HANDLE>(output_), info.wAttributes,
                               static_cast<DWORD>(width - from), at, &written);
  }
}

void Bar::write_output(std::string_view data) {
  if (!active_ || data.empty())
    return;
  if (skip_for_selection())
    return;

  erase_ours();
  place(anchor_);
  write_bytes(data);

  painted_ = false;

  Pos after{};
  int top = 0;
  if (cursor_now(after, top)) {
    anchor_.row = std::max(0, after.row);
    anchor_.column = std::max(0, after.column);
  }
  paint();
}

void Bar::set_input(std::string_view text, std::size_t cursor_columns) {
  input_text_.assign(text);
  input_cursor_columns_ = cursor_columns;
}

void Bar::set_status(std::string_view text) { status_text_.assign(text); }

void Bar::commit_line() {
  if (!active_ || skip_for_selection())
    return;

  const std::size_t end_column = std::min<std::size_t>(
      static_cast<std::size_t>(anchor_.column) + echo_columns(),
      columns_ >= 1 ? columns_ - 1 : 0);
  place(Pos{anchor_.row, static_cast<int>(end_column)});
  write_bytes("\r\n");

  Pos after{};
  int top = 0;
  if (cursor_now(after, top)) {
    anchor_.row = std::max(0, after.row);
    anchor_.column = 0;
  } else {
    anchor_.column = 0;
  }
  painted_ = false;
}

void Bar::resize() {
  if (!active_)
    return;

  CONSOLE_SCREEN_BUFFER_INFO info{};
  if (!GetConsoleScreenBufferInfo(static_cast<HANDLE>(output_), &info))
    return;
  rows_ =
      static_cast<std::size_t>(info.srWindow.Bottom - info.srWindow.Top + 1);
  columns_ =
      static_cast<std::size_t>(info.srWindow.Right - info.srWindow.Left + 1);
  if (rows_ < kMinRows)
    return;

  anchor_.row = std::min(anchor_.row, static_cast<int>(rows_) - 1);
  anchor_.column = std::min(anchor_.column, static_cast<int>(columns_) - 1);
  painted_ = false;
  paint();
}

void Bar::make_room_for_status() {

  if (anchor_.row + 1 < static_cast<int>(rows_))
    return;

  const std::size_t end_column = std::min<std::size_t>(
      static_cast<std::size_t>(anchor_.column) + echo_columns(),
      columns_ >= 1 ? columns_ - 1 : 0);
  place(Pos{anchor_.row, static_cast<int>(end_column)});
  write_bytes("\r\n");

  Pos after{};
  int top = 0;
  if (cursor_now(after, top))
    anchor_.row = std::max(0, after.row - 1);
}

void Bar::paint() {
  if (!active_)
    return;
  if (skip_for_selection())
    return;

  if (painted_ && painted_input_ == input_text_ &&
      painted_cursor_ == input_cursor_columns_ &&
      painted_status_ == status_text_)
    return;

  if (anchor_.row + 1 >= static_cast<int>(rows_))
    make_room_for_status();

  const std::size_t room =
      columns_ > static_cast<std::size_t>(anchor_.column)
          ? columns_ - static_cast<std::size_t>(anchor_.column)
          : 0;
  std::string echo;
  std::size_t cursor_columns = 0;
  {
    std::size_t i = 0;
    std::size_t columns = 0;
    while (i < input_text_.size()) {
      const text::CharWidth w = text::measure(input_text_, i);
      if (w.bytes == 0 || columns + w.columns > room)
        break;
      if (i < input_cursor_columns_)
        cursor_columns = columns + w.columns;
      echo.append(input_text_, i, w.bytes);
      columns += w.columns;
      i += w.bytes;
    }
    cursor_columns = std::min(cursor_columns, room);
  }

  const bool echo_changed = !painted_ || painted_input_ != input_text_ ||
                            painted_cursor_ != input_cursor_columns_;
  const bool status_changed = !painted_ || painted_status_ != status_text_;

  if (echo_changed) {
    place(anchor_);
    write_bytes(echo);
    write_bytes("\x1b[K");
  }

  if (status_changed) {

    place(Pos{anchor_.row + 1, 0});
    write_bytes("\x1b[7m");
    write_bytes(status_text_);
    write_bytes("\x1b[K\x1b[0m");
  }

  place(Pos{anchor_.row, static_cast<int>(cursor_columns)});

  painted_ = true;
  painted_input_ = input_text_;
  painted_cursor_ = input_cursor_columns_;
  painted_status_ = status_text_;
}

} // namespace bar
} // namespace coding
