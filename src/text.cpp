#include "text.hpp"

#include <cctype>

namespace text {

namespace {

struct Codepoint {
  char32_t value;
  std::size_t length;
};

Codepoint decode_utf8(std::string_view s, std::size_t i) noexcept {
  const auto c = static_cast<unsigned char>(s[i]);
  if (c < 0x80)
    return {static_cast<char32_t>(c), 1};

  std::size_t length = 0;
  char32_t value = 0;
  if ((c & 0xE0) == 0xC0) {
    length = 2;
    value = c & 0x1Fu;
  } else if ((c & 0xF0) == 0xE0) {
    length = 3;
    value = c & 0x0Fu;
  } else if ((c & 0xF8) == 0xF0) {
    length = 4;
    value = c & 0x07u;
  } else {
    return {0xFFFD, 1};
  }

  if (i + length > s.size())
    return {0xFFFD, 1};

  for (std::size_t k = 1; k < length; ++k) {
    const auto cc = static_cast<unsigned char>(s[i + k]);
    if ((cc & 0xC0) != 0x80)
      return {0xFFFD, 1};
    value = static_cast<char32_t>((value << 6) | (cc & 0x3Fu));
  }
  return {value, length};
}

bool is_wide(char32_t cp) noexcept {
  return (cp >= 0x1100 && cp <= 0x115F) ||   // Hangul Jamo
         (cp >= 0x2E80 && cp <= 0x303E) ||   // CJK radicals, Kangxi, symbols
         (cp >= 0x3041 && cp <= 0x33FF) ||   // kana, Bopomofo, compat Jamo
         (cp >= 0x3400 && cp <= 0x4DBF) ||   // CJK extension A
         (cp >= 0x4E00 && cp <= 0x9FFF) ||   // CJK unified ideographs
         (cp >= 0xA000 && cp <= 0xA4CF) ||   // Yi
         (cp >= 0xAC00 && cp <= 0xD7A3) ||   // Hangul syllables
         (cp >= 0xF900 && cp <= 0xFAFF) ||   // CJK compatibility ideographs
         (cp >= 0xFE10 && cp <= 0xFE19) ||   // vertical forms
         (cp >= 0xFE30 && cp <= 0xFE6F) ||   // CJK compatibility forms
         (cp >= 0xFF00 && cp <= 0xFF60) ||   // fullwidth forms
         (cp >= 0xFFE0 && cp <= 0xFFE6) ||   //
         (cp >= 0x1F300 && cp <= 0x1F64F) || // emoji
         (cp >= 0x1F900 && cp <= 0x1F9FF) || //
         (cp >= 0x20000 && cp <= 0x3FFFD);   // CJK extension B and beyond
}

bool is_zero_width(char32_t cp) noexcept {
  return cp < 0x20 || cp == 0x7F ||
         (cp >= 0x0300 && cp <= 0x036F) || // combining diacritical marks
         (cp >= 0x0483 && cp <= 0x0489) || //
         (cp >= 0x1AB0 && cp <= 0x1AFF) || // combining marks extended
         (cp >= 0x1DC0 && cp <= 0x1DFF) || // combining marks supplement
         (cp >= 0x200B && cp <= 0x200F) || // zero width space, bidi marks
         (cp >= 0x20D0 && cp <= 0x20FF) || // combining marks for symbols
         (cp >= 0xFE00 && cp <= 0xFE0F) || // variation selectors
         (cp >= 0xFE20 && cp <= 0xFE2F) || // combining half marks
         cp == 0xFEFF;                     // BOM / zero width no-break space
}

} // namespace

CharWidth measure(std::string_view text, std::size_t index) noexcept {
  if (index >= text.size())
    return {0, 0};

  const Codepoint cp = decode_utf8(text, index);
  if (is_zero_width(cp.value))
    return {0, cp.length};
  return {is_wide(cp.value) ? std::size_t{2} : std::size_t{1}, cp.length};
}

std::size_t display_width(std::string_view text) noexcept {
  std::size_t columns = 0;
  for (std::size_t i = 0; i < text.size();) {
    const CharWidth w = measure(text, i);
    columns += w.columns;
    i += w.bytes;
  }
  return columns;
}

bool is_ascii(std::string_view text) noexcept {
  for (const char c : text) {
    if (static_cast<unsigned char>(c) >= 0x80)
      return false;
  }
  return true;
}

std::string pad_left(std::string_view text, std::size_t width, char fill) {
  const std::size_t columns = display_width(text);
  if (columns >= width)
    return std::string(text);
  return std::string(width - columns, fill) + std::string(text);
}

std::string pad_right(std::string_view text, std::size_t width, char fill) {
  const std::size_t columns = display_width(text);
  if (columns >= width)
    return std::string(text);
  std::string out(text);
  out.append(width - columns, fill);
  return out;
}

std::string_view trim_left(std::string_view text) noexcept {
  std::size_t i = 0;
  while (i < text.size() &&
         std::isspace(static_cast<unsigned char>(text[i])) != 0)
    ++i;
  return text.substr(i);
}

std::string_view trim_right(std::string_view text) noexcept {
  std::size_t n = text.size();
  while (n > 0 && std::isspace(static_cast<unsigned char>(text[n - 1])) != 0)
    --n;
  return text.substr(0, n);
}

std::string_view trim(std::string_view text) noexcept {
  return trim_right(trim_left(text));
}

std::string to_lower(std::string_view text) {
  std::string out(text);
  for (char &c : out)
    c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  return out;
}

std::string to_upper(std::string_view text) {
  std::string out(text);
  for (char &c : out)
    c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
  return out;
}

bool iequals(std::string_view a, std::string_view b) noexcept {
  if (a.size() != b.size())
    return false;
  for (std::size_t i = 0; i < a.size(); ++i) {
    if (std::tolower(static_cast<unsigned char>(a[i])) !=
        std::tolower(static_cast<unsigned char>(b[i])))
      return false;
  }
  return true;
}

} // namespace text
