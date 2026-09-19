#include "color.hpp"

#include <atomic>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <map>
#include <ostream>
#include <string>
#include <vector>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <unistd.h>
#endif

namespace color {

namespace {

std::atomic<bool> g_out{false};
std::atomic<bool> g_err{false};

#ifdef _WIN32
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
#else
bool is_tty(int fd) { return ::isatty(fd) != 0; }
#endif

int sgr_group(Code c) noexcept {
  int v = static_cast<int>(c);
  if (v == 1 || v == 2)
    return 1;
  if (v == 3 || v == 4 || v == 5 || v == 7 || v == 8)
    return v;
  if ((v >= 30 && v <= 37) || (v >= 90 && v <= 97))
    return 30;
  if ((v >= 40 && v <= 47) || (v >= 100 && v <= 107))
    return 40;
  return 0;
}

int group_reset(int g) noexcept {
  switch (g) {
  case 1:
    return 22;
  case 3:
    return 23;
  case 4:
    return 24;
  case 5:
    return 25;
  case 7:
    return 27;
  case 8:
    return 28;
  case 30:
    return 39;
  case 40:
    return 49;
  default:
    return 0;
  }
}

std::string sgr_set(const Code *codes, std::size_t n) {
  std::string out = "\033[";
  for (std::size_t i = 0; i < n; ++i) {
    if (i)
      out += ';';
    out += std::to_string(static_cast<int>(codes[i]));
  }
  out += 'm';
  return out;
}

std::string reset_seq(const Code *codes, std::size_t n) {
  bool bold_dim = false, italic = false, underline = false, blink = false;
  bool reverse = false, hidden = false, fg = false, bg = false, any = false;

  for (std::size_t i = 0; i < n; ++i) {
    switch (static_cast<int>(codes[i])) {
    case 0:
      return "\033[0m";
    case 1:
    case 2:
      bold_dim = true;
      any = true;
      break;
    case 3:
      italic = true;
      any = true;
      break;
    case 4:
      underline = true;
      any = true;
      break;
    case 5:
      blink = true;
      any = true;
      break;
    case 7:
      reverse = true;
      any = true;
      break;
    case 8:
      hidden = true;
      any = true;
      break;
    default: {
      int v = static_cast<int>(codes[i]);
      if ((v >= 30 && v <= 37) || (v >= 90 && v <= 97)) {
        fg = true;
        any = true;
      } else if ((v >= 40 && v <= 47) || (v >= 100 && v <= 107)) {
        bg = true;
        any = true;
      }
    }
    }
  }
  if (!any)
    return {};

  std::string out = "\033[";
  bool first = true;
  auto add = [&](int v) {
    if (!first)
      out += ';';
    first = false;
    out += std::to_string(v);
  };
  if (bold_dim)
    add(22);
  if (italic)
    add(23);
  if (underline)
    add(24);
  if (blink)
    add(25);
  if (reverse)
    add(27);
  if (hidden)
    add(28);
  if (fg)
    add(39);
  if (bg)
    add(49);
  out += 'm';
  return out;
}

std::string transition_seq(const std::vector<Code> &from,
                           const std::vector<Code> &to) {
  std::map<int, Code> fm, tm;
  for (Code c : from) {
    int g = sgr_group(c);
    if (g)
      fm[g] = c;
  }
  for (Code c : to) {
    int g = sgr_group(c);
    if (g)
      tm[g] = c;
  }

  std::string rst, set;
  for (auto &[g, c] : fm) {
    auto it = tm.find(g);
    if (it == tm.end() || it->second != c) {
      int r = group_reset(g);
      if (r) {
        if (!rst.empty())
          rst += ';';
        rst += std::to_string(r);
      }
    }
  }
  for (auto &[g, c] : tm) {
    auto it = fm.find(g);
    if (it == fm.end() || it->second != c) {
      if (!set.empty())
        set += ';';
      set += std::to_string(static_cast<int>(c));
    }
  }

  std::string out;
  if (!rst.empty())
    out += "\033[" + rst + "m";
  if (!set.empty())
    out += "\033[" + set + "m";
  return out;
}

thread_local std::map<std::ostream *, std::vector<Code>> t_state;

} // namespace

bool enabled() noexcept { return enabled(std::cout); }

bool enabled(std::ostream &os) noexcept {
  if (&os == &std::cerr || &os == &std::clog)
    return g_err.load(std::memory_order_relaxed);
  return g_out.load(std::memory_order_relaxed);
}

void set_enabled(bool on) noexcept {
  g_out.store(on, std::memory_order_relaxed);
  g_err.store(on, std::memory_order_relaxed);
}

void set_enabled(std::ostream &os, bool on) noexcept {
  if (&os == &std::cerr || &os == &std::clog)
    g_err.store(on, std::memory_order_relaxed);
  else
    g_out.store(on, std::memory_order_relaxed);
}

bool install() noexcept {
  if (const char *nc = std::getenv("NO_COLOR"); nc && *nc) {
    set_enabled(false);
    return false;
  }
  bool force = false;
  if (const char *fc = std::getenv("FORCE_COLOR");
      fc && *fc && std::strcmp(fc, "0") != 0)
    force = true;
  if (const char *cf = std::getenv("CLICOLOR_FORCE");
      cf && *cf && std::strcmp(cf, "0") != 0)
    force = true;

  if (force) {
    set_enabled(true);
    return true;
  }

#ifdef _WIN32
  const bool ok_out = try_enable_vt(GetStdHandle(STD_OUTPUT_HANDLE));
  const bool ok_err = try_enable_vt(GetStdHandle(STD_ERROR_HANDLE));
#else
  const bool ok_out = is_tty(STDOUT_FILENO);
  const bool ok_err = is_tty(STDERR_FILENO);
#endif
  g_out.store(ok_out, std::memory_order_relaxed);
  g_err.store(ok_err, std::memory_order_relaxed);
  return ok_out || ok_err;
}

std::string paint(std::string_view text, const Code *codes, std::size_t count) {
  return paint(std::cout, text, codes, count);
}

std::string paint(std::ostream &os, std::string_view text, const Code *codes,
                  std::size_t count) {
  if (count == 0 || text.empty() || !enabled(os))
    return std::string(text);

  std::string out;
  out.reserve(text.size() + count * 4 + 16);
  out += sgr_set(codes, count);
  out.append(text.data(), text.size());
  out += reset_seq(codes, count);
  return out;
}

std::string paint(std::string_view text, std::initializer_list<Code> codes) {
  return paint(std::cout, text, codes.begin(), codes.size());
}

std::string paint(std::ostream &os, std::string_view text,
                  std::initializer_list<Code> codes) {
  return paint(os, text, codes.begin(), codes.size());
}

std::ostream &operator<<(std::ostream &os, const Painted &p) {
  const auto sz = static_cast<std::streamsize>(p.text_.size());
  if (p.count_ == 0 || p.text_.empty() || !enabled(os)) {
    os.write(p.text_.data(), sz);
    return os;
  }
  const std::string set = sgr_set(p.codes_, p.count_);
  const std::string rst = reset_seq(p.codes_, p.count_);
  os.write(set.data(), static_cast<std::streamsize>(set.size()));
  os.write(p.text_.data(), sz);
  os.write(rst.data(), static_cast<std::streamsize>(rst.size()));
  return os;
}

Scope::Scope(std::ostream &os, std::initializer_list<Code> codes) {
  if (codes.size() == 0 || !enabled(os))
    return;
  os_ = &os;

  auto &state = t_state[&os];
  prev_ = state;

  std::map<int, Code> m;
  for (Code c : state) {
    int g = sgr_group(c);
    if (g)
      m[g] = c;
  }
  for (Code c : codes) {
    int g = sgr_group(c);
    if (g)
      m[g] = c;
  }

  std::vector<Code> next;
  next.reserve(m.size());
  for (auto &[g, c] : m)
    next.push_back(c);

  const std::string seq = transition_seq(state, next);
  if (!seq.empty())
    os.write(seq.data(), static_cast<std::streamsize>(seq.size()));

  state = std::move(next);
  active_ = true;
}

Scope::Scope(Scope &&other) noexcept
    : os_(other.os_), prev_(std::move(other.prev_)), active_(other.active_) {
  other.os_ = nullptr;
  other.active_ = false;
}

void Scope::release() noexcept {
  if (!active_)
    return;
  active_ = false;
  if (!os_)
    return;
  std::ostream &os = *os_;
  os_ = nullptr;

  auto it = t_state.find(&os);
  if (it == t_state.end())
    return;
  auto &state = it->second;

  const std::string seq = transition_seq(state, prev_);
  if (!seq.empty())
    os.write(seq.data(), static_cast<std::streamsize>(seq.size()));

  if (prev_.empty())
    t_state.erase(it);
  else
    state = prev_;
}

Scope::~Scope() { release(); }

std::size_t visible_width(std::string_view s) noexcept {
  std::size_t w = 0;
  std::size_t i = 0;
  const std::size_t n = s.size();
  while (i < n) {
    if (static_cast<unsigned char>(s[i]) == 0x1B && i + 1 < n &&
        s[i + 1] == '[') {
      i += 2;
      while (i < n && s[i] != 'm')
        ++i;
      if (i < n)
        ++i;
      continue;
    }
    ++w;
    ++i;
  }
  return w;
}

} // namespace color