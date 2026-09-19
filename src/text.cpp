#include "text.hpp"
#include "text_width_table.hpp"

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

template <std::size_t N>
bool in_ranges(const width_table::Range (&table)[N], char32_t cp) noexcept {
  std::size_t lo = 0;
  std::size_t hi = N;
  while (lo < hi) {
    const std::size_t mid = lo + (hi - lo) / 2;
    if (cp < table[mid].lo)
      hi = mid;
    else if (cp > table[mid].hi)
      lo = mid + 1;
    else
      return true;
  }
  return false;
}

bool is_wide(char32_t cp) noexcept { return in_ranges(width_table::kWide, cp); }

bool is_zero_width(char32_t cp) noexcept {
  return in_ranges(width_table::kZero, cp);
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
