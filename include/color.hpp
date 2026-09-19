#ifndef COLOR_HPP
#define COLOR_HPP

#include <cstddef>
#include <iosfwd>
#include <initializer_list>
#include <string>
#include <string_view>
#include <ostream>
#include <vector>

namespace color {

enum class Code {
  Reset = 0,
  Bold = 1,
  Dim = 2,
  Italic = 3,
  Underline = 4,
  Blink = 5,
  Reverse = 7,
  Hidden = 8,

  Black = 30,
  Red = 31,
  Green = 32,
  Yellow = 33,
  Blue = 34,
  Magenta = 35,
  Cyan = 36,
  White = 37,
  DefaultFg = 39,

  BrightBlack = 90,
  BrightRed = 91,
  BrightGreen = 92,
  BrightYellow = 93,
  BrightBlue = 94,
  BrightMagenta = 95,
  BrightCyan = 96,
  BrightWhite = 97,

  BgBlack = 40,
  BgRed = 41,
  BgGreen = 42,
  BgYellow = 43,
  BgBlue = 44,
  BgMagenta = 45,
  BgCyan = 46,
  BgWhite = 47,
  BgDefault = 49,

  BgBrightBlack = 100,
  BgBrightRed = 101,
  BgBrightGreen = 102,
  BgBrightYellow = 103,
  BgBrightBlue = 104,
  BgBrightMagenta = 105,
  BgBrightCyan = 106,
  BgBrightWhite = 107,
};

bool enabled() noexcept;
bool enabled(std::ostream &os) noexcept;
void set_enabled(bool on) noexcept;
void set_enabled(std::ostream &os, bool on) noexcept;

bool install() noexcept;

std::string paint(std::string_view text, std::initializer_list<Code> codes);
std::string paint(std::string_view text, const Code *codes, std::size_t count);
std::string paint(std::ostream &os, std::string_view text,
                  std::initializer_list<Code> codes);
std::string paint(std::ostream &os, std::string_view text, const Code *codes,
                  std::size_t count);

class Painted;
std::ostream &operator<<(std::ostream &os, const Painted &p);

class Painted {
public:
  static constexpr std::size_t kMaxCodes = 4;

  Painted(std::string_view text, std::initializer_list<Code> codes) noexcept
      : text_(text),
        count_(codes.size() < kMaxCodes ? codes.size() : kMaxCodes) {
    std::size_t i = 0;
    for (Code c : codes) {
      if (i >= count_)
        break;
      codes_[i++] = c;
    }
  }

  operator std::string() const { return paint(text_, codes_, count_); }

private:
  std::string_view text_;
  Code codes_[kMaxCodes] = {};
  std::size_t count_ = 0;

  friend std::ostream &operator<<(std::ostream &os, const Painted &p);
};

inline std::string bold(std::string_view s) { return paint(s, {Code::Bold}); }
inline std::string dim(std::string_view s) { return paint(s, {Code::Dim}); }

inline std::string red(std::string_view s) { return paint(s, {Code::Red}); }
inline std::string green(std::string_view s) { return paint(s, {Code::Green}); }
inline std::string yellow(std::string_view s) {
  return paint(s, {Code::Yellow});
}
inline std::string blue(std::string_view s) { return paint(s, {Code::Blue}); }
inline std::string magenta(std::string_view s) {
  return paint(s, {Code::Magenta});
}
inline std::string cyan(std::string_view s) { return paint(s, {Code::Cyan}); }
inline std::string gray(std::string_view s) {
  return paint(s, {Code::BrightBlack});
}

inline std::string ok(std::string_view s) {
  return paint(s, {Code::Bold, Code::Green});
}
inline std::string warn(std::string_view s) {
  return paint(s, {Code::Bold, Code::Yellow});
}
inline std::string err(std::string_view s) {
  return paint(s, {Code::Bold, Code::Red});
}
inline std::string info(std::string_view s) { return paint(s, {Code::Cyan}); }

class Scope {
public:
  Scope(std::ostream &os, std::initializer_list<Code> codes);
  ~Scope();

  Scope(Scope &&other) noexcept;
  Scope &operator=(Scope &&) = delete;
  Scope(const Scope &) = delete;
  Scope &operator=(const Scope &) = delete;

private:
  void release() noexcept;
  std::ostream *os_ = nullptr;
  std::vector<Code> prev_;
  bool active_ = false;
};

std::size_t visible_width(std::string_view s) noexcept;

} // namespace color

#endif // COLOR_HPP