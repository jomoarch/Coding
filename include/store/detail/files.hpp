#ifndef STORE_DETAIL_FILES_HPP
#define STORE_DETAIL_FILES_HPP

#include <cstdint>
#include <ctime>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace store::detail {

bool read_lines(const std::filesystem::path &path,
                std::vector<std::string> &out, std::string &error);

bool write_file_atomic(const std::filesystem::path &path, std::string_view text,
                       std::string &error);

std::string local_time(std::time_t when);

bool file_size_of(const std::filesystem::path &path, std::uint64_t &size,
                  std::string &error);

} // namespace store::detail

#endif // STORE_DETAIL_FILES_HPP
