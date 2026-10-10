#include "app/console_bar.hpp"

#include "app/line_edit.hpp"
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
  show_cursor();
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

void Bar::hide_cursor() {
  if (cursor_hidden_)
    return;
  cursor_hidden_ = true;
  write_bytes("\x1b[?25l");
}

void Bar::show_cursor() {
  if (!cursor_hidden_)
    return;
  cursor_hidden_ = false;
  write_bytes("\x1b[?25h");
}

void Bar::write_output(std::string_view data) {
  if (!active_ || data.empty())
    return;
  if (skip_for_selection())
    return;

  hide_cursor();

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

void Bar::set_input(std::string_view text, std::size_t cursor_bytes) {
  input_text_.assign(text);
  input_cursor_bytes_ = cursor_bytes;
}

void Bar::set_status(std::string_view text) { status_text_.assign(text); }

void Bar::commit_text(std::string_view text) {
  if (!active_ || text.empty() || skip_for_selection())
    return;

  hide_cursor();
  place(anchor_);
  write_bytes(text);
  if (text.back() != '\n')
    write_bytes("\r\n");

  Pos after{};
  int top = 0;
  if (cursor_now(after, top)) {
    anchor_.row = std::max(0, after.row);
    anchor_.column = std::max(0, after.column);
  } else {
    anchor_.column = 0;
  }

  input_text_.clear();
  input_cursor_bytes_ = 0;
  painted_ = false;
  paint();
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

void Bar::ensure_room(std::size_t echo_rows) {
  const int need = static_cast<int>(echo_rows);
  if (anchor_.row + need < static_cast<int>(rows_))
    return;

  int top_before = 0;
  Pos at{};
  cursor_now(at, top_before);

  place(Pos{static_cast<int>(rows_) - 1, 0});
  const int push = anchor_.row + need - (static_cast<int>(rows_) - 1);
  for (int i = 0; i < push; ++i)
    write_bytes("\r\n");

  int top_after = 0;
  Pos after{};
  if (cursor_now(after, top_after))
    anchor_.row -= (top_after - top_before);
  if (anchor_.row < 0)
    anchor_.row = 0;
}
void Bar::paint() {
  if (!active_)
    return;
  if (skip_for_selection())
    return;

  if (painted_ && painted_input_ == input_text_ &&
      painted_cursor_ == input_cursor_bytes_ && painted_status_ == status_text_)
    return;

  hide_cursor();

  const std::size_t first_room =
      columns_ > static_cast<std::size_t>(anchor_.column)
          ? columns_ - static_cast<std::size_t>(anchor_.column)
          : 0;
  const line::Layout laid =
      line::layout_text(input_text_, input_cursor_bytes_, first_room, columns_);

  ensure_room(laid.rows.size());

  const bool echo_changed = !painted_ || painted_input_ != input_text_ ||
                            painted_cursor_ != input_cursor_bytes_;
  const bool status_changed = !painted_ || painted_status_ != status_text_;

  if (echo_changed) {
    for (std::size_t i = 0; i < laid.rows.size(); ++i) {
      place(
          Pos{anchor_.row + static_cast<int>(i), i == 0 ? anchor_.column : 0});
      write_bytes(laid.rows[i]);
      write_bytes("\x1b[K");
    }
  }

  if (status_changed) {
    place(Pos{anchor_.row + static_cast<int>(laid.rows.size()), 0});
    write_bytes("\x1b[7m");
    write_bytes(status_text_);
    write_bytes("\x1b[K\x1b[0m");
  }

  place(Pos{anchor_.row + static_cast<int>(laid.caret_row),
            static_cast<int>(laid.caret_column)});
  show_cursor();

  painted_ = true;
  painted_input_ = input_text_;
  painted_cursor_ = input_cursor_bytes_;
  painted_status_ = status_text_;
}
} // namespace bar
} // namespace coding
