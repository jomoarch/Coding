#include "base/terminal.hpp"

#include "base/win_error.hpp"

#include <windows.h>

#include <algorithm>
#include <string>

namespace term {

namespace {

constexpr char kAltScreenOn[] = "\x1b[?1049h";
constexpr char kAltScreenOff[] = "\x1b[?1049l";
constexpr char kCursorHide[] = "\x1b[?25l";
constexpr char kCursorShow[] = "\x1b[?25h";
constexpr char kClear[] = "\x1b[2J\x1b[H";

Session *g_active = nullptr;

BOOL WINAPI control_handler(DWORD type) {
  if (g_active == nullptr)
    return FALSE;
  if (type == CTRL_C_EVENT || type == CTRL_BREAK_EVENT ||
      type == CTRL_CLOSE_EVENT)
    g_active->close();
  return FALSE;
}

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

Session::~Session() { close(); }

Session::Session(Session &&other) noexcept { *this = std::move(other); }

Session &Session::operator=(Session &&other) noexcept {
  if (this == &other)
    return *this;
  close();
  input_ = other.input_;
  output_ = other.output_;
  saved_input_mode_ = other.saved_input_mode_;
  saved_output_cp_ = other.saved_output_cp_;
  active_ = other.active_;
  other.active_ = false;
  if (active_)
    g_active = this;
  return *this;
}

bool Session::open(Session &out, std::string &error) {
  const HANDLE in = GetStdHandle(STD_INPUT_HANDLE);
  const HANDLE os = GetStdHandle(STD_OUTPUT_HANDLE);

  DWORD in_mode = 0;
  DWORD out_mode = 0;
  if (in == nullptr || in == INVALID_HANDLE_VALUE || os == nullptr ||
      os == INVALID_HANDLE_VALUE || !GetConsoleMode(in, &in_mode) ||
      !GetConsoleMode(os, &out_mode)) {
    error = "not a console: the browser needs a terminal on stdin and stdout";
    return false;
  }

  out.input_ = in;
  out.output_ = os;
  out.saved_input_mode_ = in_mode;
  out.saved_output_cp_ = GetConsoleOutputCP();

  const DWORD new_in = (in_mode & ~(ENABLE_LINE_INPUT | ENABLE_ECHO_INPUT |
                                    ENABLE_PROCESSED_INPUT)) |
                       ENABLE_WINDOW_INPUT;
  const DWORD new_out = out_mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING;

  if (!SetConsoleMode(in, new_in) || !SetConsoleMode(os, new_out)) {
    error =
        "cannot switch the console to raw mode: " + win::last_error_string();
    return false;
  }
  SetConsoleOutputCP(CP_UTF8);

  SetConsoleCtrlHandler(control_handler, TRUE);
  g_active = &out;
  out.active_ = true;

  raw_write(os, kAltScreenOn);
  raw_write(os, kCursorHide);
  raw_write(os, kClear);
  return true;
}

void Session::close() noexcept {
  if (!active_)
    return;
  active_ = false;
  if (g_active == this)
    g_active = nullptr;

  const HANDLE os = static_cast<HANDLE>(output_);
  raw_write(os, std::string(kCursorShow) + kAltScreenOff);

  SetConsoleMode(static_cast<HANDLE>(input_),
                 static_cast<DWORD>(saved_input_mode_));
  SetConsoleOutputCP(saved_output_cp_);
  SetConsoleCtrlHandler(control_handler, FALSE);
}

Size Session::size() const {
  Size result;
  CONSOLE_SCREEN_BUFFER_INFO info{};
  if (!GetConsoleScreenBufferInfo(static_cast<HANDLE>(output_), &info))
    return result;
  result.columns =
      static_cast<std::size_t>(info.srWindow.Right - info.srWindow.Left + 1);
  result.rows =
      static_cast<std::size_t>(info.srWindow.Bottom - info.srWindow.Top + 1);
  return result;
}

void Session::write(std::string_view frame) const {
  raw_write(static_cast<HANDLE>(output_), frame);
}

bool Session::read(Key &out) const {
  const HANDLE in = static_cast<HANDLE>(input_);
  for (;;) {
    INPUT_RECORD record{};
    DWORD got = 0;
    if (!ReadConsoleInputW(in, &record, 1, &got) || got == 0)
      return false;

    if (record.EventType == WINDOW_BUFFER_SIZE_EVENT) {
      out = Key::Resize;
      return true;
    }
    if (record.EventType != KEY_EVENT || !record.Event.KeyEvent.bKeyDown)
      continue;

    const KEY_EVENT_RECORD &key = record.Event.KeyEvent;
    const bool ctrl =
        (key.dwControlKeyState & (LEFT_CTRL_PRESSED | RIGHT_CTRL_PRESSED)) != 0;
    const bool shift = (key.dwControlKeyState & SHIFT_PRESSED) != 0;

    switch (key.wVirtualKeyCode) {
    case VK_UP:
      out = ctrl ? Key::ViewUp : Key::Up;
      return true;
    case VK_DOWN:
      out = ctrl ? Key::ViewDown : Key::Down;
      return true;
    case VK_RETURN:
      out = shift ? Key::ShiftEnter : Key::Enter;
      return true;
    case VK_ESCAPE:
      out = Key::Quit;
      return true;
    case 'J':
      out = ctrl ? Key::ViewDown : Key::Down;
      return true;
    case 'K':
      out = ctrl ? Key::ViewUp : Key::Up;
      return true;
    case 'C':
      out = ctrl ? Key::Quit : Key::Collapse;
      return true;
    case 'R':
      out = Key::CollapseAll;
      return true;
    case 'Q':
      out = Key::Quit;
      return true;
    default:
      break;
    }
  }
}

} // namespace term
