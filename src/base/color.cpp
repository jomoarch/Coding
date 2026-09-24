#include "base/color.hpp"

#include <algorithm>
#include <atomic>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <map>
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
  const int v = static_cast<int>(c);
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

std::string sgr_params(const Attr *attrs, std::size_t count) {
  std::string out;
  out.reserve(count * 6);
  for (std::size_t i = 0; i < count; ++i) {
    if (i != 0)
      out += ';';
    out += attrs[i].sgr();
  }
  return out;
}

std::string sgr_set(const Attr *attrs, std::size_t count) {
  if (count == 0)
    return {};
  return "\033[" + sgr_params(attrs, count) + "m";
}

std::string reset_seq(const Attr *attrs, std::size_t count) {
  if (count == 0)
    return {};

  std::vector<int> resets;
  resets.reserve(count);

  for (std::size_t i = 0; i < count; ++i) {
    if (attrs[i].is_basic() && attrs[i].code() == Code::Reset)
      return "\033[0m";

    const int r = attrs[i].reset_group();
    if (r == 0)
      continue;
    if (std::find(resets.begin(), resets.end(), r) == resets.end())
      resets.push_back(r);
  }

  if (resets.empty())
    return {};

  std::sort(resets.begin(), resets.end());

  std::string out = "\033[";
  for (std::size_t i = 0; i < resets.size(); ++i) {
    if (i != 0)
      out += ';';
    out += std::to_string(resets[i]);
  }
  out += 'm';
  return out;
}

std::string transition_seq(const std::vector<Attr> &from,
                           const std::vector<Attr> &to) {
  std::map<int, Attr> from_slots;
  std::map<int, Attr> to_slots;
  for (const Attr &a : from) {
    const int g = a.group();
    if (g != 0)
      from_slots[g] = a;
  }
  for (const Attr &a : to) {
    const int g = a.group();
    if (g != 0)
      to_slots[g] = a;
  }

  std::vector<int> resets;
  for (const auto &entry : from_slots) {
    const auto it = to_slots.find(entry.first);
    if (it == to_slots.end() || it->second != entry.second) {
      const int r = entry.second.reset_group();
      if (r != 0)
        resets.push_back(r);
    }
  }

  std::vector<Attr> sets;
  for (const auto &entry : to_slots) {
    const auto it = from_slots.find(entry.first);
    if (it == from_slots.end() || !(it->second == entry.second))
      sets.push_back(entry.second);
  }

  std::sort(resets.begin(), resets.end());

  std::string out;
  if (!resets.empty()) {
    out += "\033[";
    for (std::size_t i = 0; i < resets.size(); ++i) {
      if (i != 0)
        out += ';';
      out += std::to_string(resets[i]);
    }
    out += 'm';
  }
  if (!sets.empty())
    out += sgr_set(sets.data(), sets.size());
  return out;
}

std::string paint_impl(std::ostream &os, std::string_view body,
                       const Attr *attrs, std::size_t count) {
  if (count == 0 || body.empty() || !enabled(os))
    return std::string(body);

  const std::string params = sgr_params(attrs, count);
  const std::string reset = reset_seq(attrs, count);

  std::string out;
  out.reserve(3 + params.size() + body.size() + reset.size());
  out += "\033[";
  out += params;
  out += 'm';
  out.append(body.data(), body.size());
  out += reset;
  return out;
}

thread_local std::map<std::ostream *, std::vector<Attr>> t_state;

} // namespace

std::string Attr::sgr() const {
  switch (kind_) {
  case Kind::Basic:
    return std::to_string(static_cast<int>(code_));
  case Kind::IndexedFg:
    return "38;5;" + std::to_string(index_);
  case Kind::IndexedBg:
    return "48;5;" + std::to_string(index_);
  case Kind::RgbFg:
    return "38;2;" + std::to_string(r_) + ";" + std::to_string(g_) + ";" +
           std::to_string(b_);
  case Kind::RgbBg:
    return "48;2;" + std::to_string(r_) + ";" + std::to_string(g_) + ";" +
           std::to_string(b_);
  }
  return {};
}

int Attr::group() const noexcept {
  switch (kind_) {
  case Kind::Basic:
    return sgr_group(code_);
  case Kind::IndexedFg:
  case Kind::RgbFg:
    return 30;
  case Kind::IndexedBg:
  case Kind::RgbBg:
    return 40;
  }
  return 0;
}

int Attr::reset_group() const noexcept {
  switch (kind_) {
  case Kind::Basic:
    return group_reset(sgr_group(code_));
  case Kind::IndexedFg:
  case Kind::RgbFg:
    return 39;
  case Kind::IndexedBg:
  case Kind::RgbBg:
    return 49;
  }
  return 0;
}

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

std::string paint(std::string_view text, std::initializer_list<Attr> attrs) {
  return paint_impl(std::cout, text, attrs.begin(), attrs.size());
}

std::string paint(std::ostream &os, std::string_view text,
                  std::initializer_list<Attr> attrs) {
  return paint_impl(os, text, attrs.begin(), attrs.size());
}

std::string strip(std::string_view text) {
  std::string out;
  out.reserve(text.size());

  std::size_t i = 0;
  const std::size_t n = text.size();
  while (i < n) {
    if (static_cast<unsigned char>(text[i]) == 0x1B && i + 1 < n &&
        text[i + 1] == '[') {
      i += 2;
      while (i < n && text[i] != 'm')
        ++i;
      if (i < n)
        ++i;
      continue;
    }
    out.push_back(text[i]);
    ++i;
  }
  return out;
}

std::string Style::render(std::string_view text_body) const {
  return paint_impl(std::cout, text_body, attrs_.data(), attrs_.size());
}

std::string Style::set_sequence(std::ostream &os) const {
  if (attrs_.empty() || !enabled(os))
    return {};
  return sgr_set(attrs_.data(), attrs_.size());
}

std::string Style::reset_sequence(std::ostream &os) const {
  if (attrs_.empty() || !enabled(os))
    return {};
  return reset_seq(attrs_.data(), attrs_.size());
}

Scope::Scope(std::ostream &os, std::initializer_list<Attr> attrs) {
  if (attrs.size() == 0 || !enabled(os))
    return;
  os_ = &os;

  auto &state = t_state[&os];
  prev_ = state;

  std::map<int, Attr> merged;
  for (const Attr &a : state) {
    const int g = a.group();
    if (g != 0)
      merged[g] = a;
  }
  for (const Attr &a : attrs) {
    const int g = a.group();
    if (g != 0)
      merged[g] = a;
  }

  std::vector<Attr> next;
  next.reserve(merged.size());
  for (const auto &entry : merged)
    next.push_back(entry.second);

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

  const auto it = t_state.find(&os);
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

std::size_t visible_width(std::string_view text) noexcept {
  std::size_t columns = 0;
  std::size_t i = 0;
  const std::size_t n = text.size();

  while (i < n) {
    if (static_cast<unsigned char>(text[i]) == 0x1B && i + 1 < n &&
        text[i + 1] == '[') {
      i += 2;
      while (i < n && text[i] != 'm')
        ++i;
      if (i < n)
        ++i;
      continue;
    }

    const text::CharWidth w = text::measure(text, i);
    columns += w.columns;
    i += w.bytes;
  }
  return columns;
}

} // namespace color
