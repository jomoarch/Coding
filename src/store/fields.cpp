#include "store/detail/fields.hpp"

#include <string>
#include <string_view>
#include <vector>

namespace store::detail {

std::string escape_field(std::string_view text) {
  std::string out;
  out.reserve(text.size());
  for (char c : text) {
    switch (c) {
    case '\\':
      out += "\\\\";
      break;
    case '\t':
      out += "\\t";
      break;
    case '\n':
      out += "\\n";
      break;
    case '\r':
      out += "\\r";
      break;
    default:
      out.push_back(c);
    }
  }
  return out;
}

std::string unescape_field(std::string_view text) {
  std::string out;
  out.reserve(text.size());
  for (std::size_t i = 0; i < text.size(); ++i) {
    if (text[i] != '\\' || i + 1 >= text.size()) {
      out.push_back(text[i]);
      continue;
    }
    switch (text[++i]) {
    case 't':
      out.push_back('\t');
      break;
    case 'n':
      out.push_back('\n');
      break;
    case 'r':
      out.push_back('\r');
      break;
    default:
      out.push_back(text[i]);
    }
  }
  return out;
}

std::vector<std::string> split_row(const std::string &line) {
  std::vector<std::string> out;
  std::size_t start = 0;
  for (std::size_t i = 0; i <= line.size(); ++i) {
    if (i == line.size() || line[i] == '\t') {
      out.push_back(
          unescape_field(std::string_view(line).substr(start, i - start)));
      start = i + 1;
    }
  }
  return out;
}

std::string join_row(const std::vector<std::string> &fields) {
  std::string out;
  for (std::size_t i = 0; i < fields.size(); ++i) {
    if (i != 0)
      out.push_back('\t');
    out += escape_field(fields[i]);
  }
  return out;
}

} // namespace store::detail
