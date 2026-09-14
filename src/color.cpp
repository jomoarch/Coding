#include "color.hpp"

#include <atomic>
#include <string>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

namespace color {

namespace {

std::atomic<bool> g_enabled{false};

bool try_enable_vt(HANDLE h) {
  if (h == nullptr || h == INVALID_HANDLE_VALUE)
    return false;

  DWORD mode = 0;
  if (!GetConsoleMode(h, &mode))
    return false;

  if (mode & ENABLE_VIRTUAL_TERMINAL_PROCESSING)
    return true;

  return SetConsoleMode(h, mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING) != FALSE;
}

} // namespace

bool enabled() noexcept { return g_enabled.load(std::memory_order_relaxed); }

void set_enabled(bool on) noexcept {
  g_enabled.store(on, std::memory_order_relaxed);
}

bool install() noexcept {
  const bool ok_out = try_enable_vt(GetStdHandle(STD_OUTPUT_HANDLE));
  const bool ok_err = try_enable_vt(GetStdHandle(STD_ERROR_HANDLE));
  const bool ok = ok_out || ok_err;
  g_enabled.store(ok, std::memory_order_relaxed);
  return ok;
}

std::string paint(std::string_view text, std::initializer_list<Code> codes) {
  if (codes.size() == 0 || !enabled())
    return std::string(text);

  std::string out;
  out.reserve(text.size() + codes.size() * 4 + 8);

  out += '\x1b[';
  bool first = true;
  for (Code c : codes) {
    if (!first)
      out += ';';
    first = false;
    out += std::to_string(static_cast<int>(c));
  }
  out += 'm';
  out.append(text.data(), text.size());
  out += "\x1b[0m";
  return out;
}

} // namespace color