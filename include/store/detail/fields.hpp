#ifndef STORE_DETAIL_FIELDS_HPP
#define STORE_DETAIL_FIELDS_HPP

#include <string>
#include <string_view>
#include <vector>

namespace store::detail {

std::string join_row(const std::vector<std::string> &fields);
std::vector<std::string> split_row(const std::string &line);

std::string escape_field(std::string_view text);
std::string unescape_field(std::string_view text);

} // namespace store::detail

#endif // STORE_DETAIL_FIELDS_HPP
