#ifndef COLOR_HPP
#define COLOR_HPP

#include <initializer_list>
#include <string>
#include <string_view>

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
void set_enabled(bool on) noexcept;

bool install() noexcept;

std::string paint(std::string_view text, std::initializer_list<Code> codes);

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

} // namespace color

#endif // COLOR_HPP