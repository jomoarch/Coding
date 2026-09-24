#ifndef TEXT_HPP
#define TEXT_HPP

#include <cstddef>
#include <filesystem>
#include <ostream>
#include <sstream>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>

namespace text {

namespace detail {

template <class T> using bare_t = std::remove_cv_t<std::remove_reference_t<T>>;

template <class T> inline constexpr bool always_false_v = false;

template <class T, class = void> struct is_streamable : std::false_type {};

template <class T>
struct is_streamable<T, std::void_t<decltype(std::declval<std::ostream &>()
                                             << std::declval<const T &>())>>
    : std::true_type {};

template <class T>
inline constexpr bool is_text_v =
    std::is_convertible_v<const T &, std::string_view>;

template <class T>
inline constexpr bool is_char_pointer_v =
    std::is_same_v<T, char *> || std::is_same_v<T, const char *>;

template <class T>
inline constexpr bool is_direct_text_v = is_text_v<T> && !is_char_pointer_v<T>;

} // namespace detail

template <class T> std::string to_text(const T &value) {
  using U = detail::bare_t<T>;

  if constexpr (std::is_same_v<U, std::filesystem::path>) {
    const auto u8 = value.u8string();
    return std::string(u8.begin(), u8.end());
  } else if constexpr (detail::is_char_pointer_v<U>) {
    return value ? std::string(value) : std::string();
  } else if constexpr (detail::is_text_v<U>) {
    return std::string(std::string_view(value));
  } else if constexpr (std::is_same_v<U, bool>) {
    return value ? "true" : "false";
  } else if constexpr (std::is_same_v<U, char>) {
    return std::string(1, value);
  } else if constexpr (std::is_integral_v<U>) {
    return std::to_string(value);
  } else if constexpr (std::is_floating_point_v<U>) {
    std::ostringstream oss;
    oss << value;
    return oss.str();
  } else if constexpr (detail::is_streamable<U>::value) {
    std::ostringstream oss;
    oss << value;
    return oss.str();
  } else {
    static_assert(detail::always_false_v<U>,
                  "text::to_text: this type cannot be converted to text. "
                  "Add an operator<<(std::ostream&, const T&) overload, or "
                  "pass a std::string instead.");
    return std::string();
  }
}

namespace detail {

template <class T> void append_part(std::string &out, const T &value) {
  using U = bare_t<T>;
  if constexpr (is_direct_text_v<U>) {
    out.append(std::string_view(value));
  } else if constexpr (std::is_same_v<U, char>) {
    out.push_back(value);
  } else {
    out += to_text(value);
  }
}

} // namespace detail

template <class... Parts> std::string concat(Parts &&...parts) {
  std::string out;
  out.reserve(sizeof...(Parts) * 8);
  (detail::append_part(out, parts), ...);
  return out;
}

struct CharWidth {
  std::size_t columns{0};
  std::size_t bytes{0};
};

CharWidth measure(std::string_view text, std::size_t index) noexcept;

std::size_t display_width(std::string_view text) noexcept;

bool is_ascii(std::string_view text) noexcept;

std::string pad_left(std::string_view text, std::size_t width, char fill = ' ');
std::string pad_right(std::string_view text, std::size_t width,
                      char fill = ' ');

std::string_view trim(std::string_view text) noexcept;
std::string_view trim_left(std::string_view text) noexcept;
std::string_view trim_right(std::string_view text) noexcept;

std::string to_lower(std::string_view text);
std::string to_upper(std::string_view text);

bool iequals(std::string_view a, std::string_view b) noexcept;

} // namespace text

#endif // TEXT_HPP
