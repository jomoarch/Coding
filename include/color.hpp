#ifndef COLOR_HPP
#define COLOR_HPP

#include "text.hpp"

#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <iosfwd>
#include <ostream>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
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

class Attr {
public:
  enum class Kind : std::uint8_t {
    Basic,
    IndexedFg,
    IndexedBg,
    RgbFg,
    RgbBg,
  };

  Attr() noexcept = default;

  Attr(Code code) noexcept : kind_(Kind::Basic), code_(code) {}

  static Attr indexed_fg(std::uint8_t index) noexcept {
    return make_indexed(Kind::IndexedFg, index);
  }
  static Attr indexed_bg(std::uint8_t index) noexcept {
    return make_indexed(Kind::IndexedBg, index);
  }
  static Attr fg(std::uint8_t r, std::uint8_t g, std::uint8_t b) noexcept {
    return make_rgb(Kind::RgbFg, r, g, b);
  }
  static Attr bg(std::uint8_t r, std::uint8_t g, std::uint8_t b) noexcept {
    return make_rgb(Kind::RgbBg, r, g, b);
  }

  Kind kind() const noexcept { return kind_; }
  Code code() const noexcept { return code_; }
  bool is_basic() const noexcept { return kind_ == Kind::Basic; }

  std::string sgr() const;

  int group() const noexcept;

  int reset_group() const noexcept;

  bool operator==(const Attr &other) const noexcept {
    return kind_ == other.kind_ && code_ == other.code_ &&
           index_ == other.index_ && r_ == other.r_ && g_ == other.g_ &&
           b_ == other.b_;
  }
  bool operator!=(const Attr &other) const noexcept {
    return !(*this == other);
  }

private:
  static Attr make_indexed(Kind kind, std::uint8_t index) noexcept {
    Attr a;
    a.kind_ = kind;
    a.index_ = index;
    return a;
  }

  static Attr make_rgb(Kind kind, std::uint8_t r, std::uint8_t g,
                       std::uint8_t b) noexcept {
    Attr a;
    a.kind_ = kind;
    a.r_ = r;
    a.g_ = g;
    a.b_ = b;
    return a;
  }

  Kind kind_{Kind::Basic};
  Code code_{Code::Reset};
  std::uint8_t index_{0};
  std::uint8_t r_{0};
  std::uint8_t g_{0};
  std::uint8_t b_{0};
};

bool enabled() noexcept;
bool enabled(std::ostream &os) noexcept;
void set_enabled(bool on) noexcept;
void set_enabled(std::ostream &os, bool on) noexcept;

bool install() noexcept;

std::string paint(std::string_view text, std::initializer_list<Attr> attrs);
std::string paint(std::ostream &os, std::string_view text,
                  std::initializer_list<Attr> attrs);

template <class T,
          std::enable_if_t<!std::is_convertible_v<const T &, std::string_view>,
                           int> = 0>
std::string paint(const T &value, std::initializer_list<Attr> attrs) {
  return paint(text::to_text(value), attrs);
}

template <class T,
          std::enable_if_t<!std::is_convertible_v<const T &, std::string_view>,
                           int> = 0>
std::string paint(std::ostream &os, const T &value,
                  std::initializer_list<Attr> attrs) {
  return paint(os, text::to_text(value), attrs);
}

std::string strip(std::string_view text);

namespace detail {

template <class... Parts>
std::string styled(std::initializer_list<Attr> attrs, Parts &&...parts) {
  return paint(text::concat(std::forward<Parts>(parts)...), attrs);
}

} // namespace detail

template <class... Parts> std::string bold(Parts &&...parts) {
  return detail::styled({Code::Bold}, std::forward<Parts>(parts)...);
}
template <class... Parts> std::string dim(Parts &&...parts) {
  return detail::styled({Code::Dim}, std::forward<Parts>(parts)...);
}
template <class... Parts> std::string red(Parts &&...parts) {
  return detail::styled({Code::Red}, std::forward<Parts>(parts)...);
}
template <class... Parts> std::string green(Parts &&...parts) {
  return detail::styled({Code::Green}, std::forward<Parts>(parts)...);
}
template <class... Parts> std::string yellow(Parts &&...parts) {
  return detail::styled({Code::Yellow}, std::forward<Parts>(parts)...);
}
template <class... Parts> std::string blue(Parts &&...parts) {
  return detail::styled({Code::Blue}, std::forward<Parts>(parts)...);
}
template <class... Parts> std::string magenta(Parts &&...parts) {
  return detail::styled({Code::Magenta}, std::forward<Parts>(parts)...);
}
template <class... Parts> std::string cyan(Parts &&...parts) {
  return detail::styled({Code::Cyan}, std::forward<Parts>(parts)...);
}
template <class... Parts> std::string gray(Parts &&...parts) {
  return detail::styled({Code::BrightBlack}, std::forward<Parts>(parts)...);
}

template <class... Parts> std::string ok(Parts &&...parts) {
  return detail::styled({Code::Bold, Code::Green},
                        std::forward<Parts>(parts)...);
}
template <class... Parts> std::string warn(Parts &&...parts) {
  return detail::styled({Code::Bold, Code::Yellow},
                        std::forward<Parts>(parts)...);
}
template <class... Parts> std::string err(Parts &&...parts) {
  return detail::styled({Code::Bold, Code::Red}, std::forward<Parts>(parts)...);
}
template <class... Parts> std::string info(Parts &&...parts) {
  return detail::styled({Code::Cyan}, std::forward<Parts>(parts)...);
}

class Style {
public:
  Style() = default;
  Style(std::initializer_list<Attr> attrs) : attrs_(attrs) {}

  const std::vector<Attr> &attrs() const noexcept { return attrs_; }
  bool empty() const noexcept { return attrs_.empty(); }

  template <class... Parts> std::string operator()(Parts &&...parts) const {
    return render(text::concat(std::forward<Parts>(parts)...));
  }

  template <class... Parts>
  void write(std::ostream &os, Parts &&...parts) const {
    const std::string body = text::concat(std::forward<Parts>(parts)...);
    const std::string set = set_sequence(os);
    if (set.empty()) {
      os.write(body.data(), static_cast<std::streamsize>(body.size()));
      return;
    }
    const std::string reset = reset_sequence(os);
    os.write(set.data(), static_cast<std::streamsize>(set.size()));
    os.write(body.data(), static_cast<std::streamsize>(body.size()));
    os.write(reset.data(), static_cast<std::streamsize>(reset.size()));
  }

private:
  std::string render(std::string_view text_body) const;
  std::string set_sequence(std::ostream &os) const;
  std::string reset_sequence(std::ostream &os) const;

  std::vector<Attr> attrs_;
};

inline Style style(std::initializer_list<Attr> attrs) { return Style(attrs); }

class Scope {
public:
  Scope(std::ostream &os, std::initializer_list<Attr> attrs);
  ~Scope();

  Scope(Scope &&other) noexcept;
  Scope &operator=(Scope &&) = delete;
  Scope(const Scope &) = delete;
  Scope &operator=(const Scope &) = delete;

private:
  void release() noexcept;
  std::ostream *os_ = nullptr;
  std::vector<Attr> prev_;
  bool active_ = false;
};

std::size_t visible_width(std::string_view text) noexcept;

} // namespace color

#endif // COLOR_HPP
