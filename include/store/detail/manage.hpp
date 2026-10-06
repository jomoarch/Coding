#ifndef STORE_DETAIL_MANAGE_HPP
#define STORE_DETAIL_MANAGE_HPP

#include "store/detail/index.hpp"

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>
namespace coding {

namespace store::detail {

bool take_row(const std::filesystem::path &dir, const Layout &layout,
              const std::string &id, Row &out, std::string &error);

void insert_by_time(std::vector<Row> &rows, Row row, const Layout &layout);

bool move_folder(const std::filesystem::path &from,
                 const std::filesystem::path &to, std::string &error);

std::uintmax_t erase_id_folders(const std::filesystem::path &dir);

std::uintmax_t id_folders_bytes(const std::filesystem::path &dir);

std::int64_t row_time(const Row &row, const Layout &layout);

} // namespace store::detail

} // namespace coding

#endif // STORE_DETAIL_MANAGE_HPP
