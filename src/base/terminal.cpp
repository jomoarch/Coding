#include "base/terminal.hpp"

#include "base/win_error.hpp"

#include <windows.h>

#include <algorithm>
#include <string>
namespace coding {

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
  alt_ = other.alt_;
  other.active_ = false;
  if (active_)
    g_active = this;
  return *this;
}

bool Session::open(Session &out, std::string &error, Screen screen) {
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

  out.alt_ = screen == Screen::Alternate;
  if (out.alt_) {
    raw_write(os, kAltScreenOn);
    raw_write(os, kCursorHide);
    raw_write(os, kClear);
  }
  return true;
}

void Session::close() noexcept {
  if (!active_)
    return;
  active_ = false;
  if (g_active == this)
    g_active = nullptr;

  const HANDLE os = static_cast<HANDLE>(output_);
  if (alt_)
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
  return read_key(out, false, nullptr, kWaitForever);
}

bool Session::read_any(Key &out, char32_t &text) const {
  return read_key(out, true, &text, kWaitForever);
}

bool Session::read_any(Key &out, char32_t &text, unsigned timeout_ms) const {
  return read_key(out, true, &text, timeout_ms);
}

bool Session::read_key(Key &out, bool raw, char32_t *text,
                       unsigned timeout_ms) const {
  const HANDLE in = static_cast<HANDLE>(input_);
  for (;;) {
    if (WaitForSingleObject(in, static_cast<DWORD>(timeout_ms)) !=
        WAIT_OBJECT_0)
      return false;

    if (timeout_ms != kWaitForever)
      timeout_ms = 0;

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

    if (raw && text != nullptr) {
      const wchar_t unit = key.uChar.UnicodeChar;

      if (unit == 0x0A) {
        out = Key::ViewDown;
        return true;
      }
      if (unit == 0x0B) {
        out = Key::ViewUp;
        return true;
      }
      if (unit == 0x03) {
        out = Key::Quit;
        return true;
      }

      if (unit >= 0x20 && unit != 0x7F) {
        char32_t codepoint = unit;
        if (unit >= 0xD800 && unit <= 0xDBFF) {
          INPUT_RECORD next{};
          DWORD next_got = 0;
          if (ReadConsoleInputW(in, &next, 1, &next_got) && next_got != 0 &&
              next.EventType == KEY_EVENT &&
              next.Event.KeyEvent.uChar.UnicodeChar >= 0xDC00 &&
              next.Event.KeyEvent.uChar.UnicodeChar <= 0xDFFF) {
            codepoint =
                0x10000 + ((static_cast<char32_t>(unit) - 0xD800) << 10) +
                (static_cast<char32_t>(next.Event.KeyEvent.uChar.UnicodeChar) -
                 0xDC00);
          }
        }
        *text = codepoint;
        out = Key::Text;
        return true;
      }
    }

    switch (key.wVirtualKeyCode) {
    case VK_UP:
      out = ctrl ? Key::ViewUp : Key::Up;
      return true;
    case VK_DOWN:
      out = ctrl ? Key::ViewDown : Key::Down;
      return true;
    case VK_LEFT:
      out = Key::Left;
      return true;
    case VK_RIGHT:
      out = Key::Right;
      return true;
    case VK_RETURN:
      out = shift ? Key::ShiftEnter : Key::Enter;
      return true;
    case VK_BACK:
      out = Key::Backspace;
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
    case 'V':
      // Only reached for Ctrl+V: a plain 'v' comes back as text above.
      out = Key::Paste;
      return true;
    case VK_INSERT:
      if (shift) {
        out = Key::Paste;
        return true;
      }
      break;
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
} // namespace coding
