#include "store/detail/manage.hpp"

#include "store/detail/archive.hpp"

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

namespace store::detail {

namespace fs = std::filesystem;

bool take_row(const fs::path &dir, const Layout &layout, const std::string &id,
              Row &out, std::string &error) {
  std::vector<Row> rows;
  if (!read(dir, layout, rows, error))
    return false;

  const auto found =
      std::find_if(rows.begin(), rows.end(), [&id, &layout](const Row &row) {
        return row[layout.id_column] == id;
      });
  if (found == rows.end()) {
    error = "No record with id " + id;
    return false;
  }

  out = *found;
  rows.erase(found);
  return write(dir, layout, rows, error);
}

std::int64_t row_time(const Row &row, const Layout &layout) {
  try {
    return std::stoll(row[layout.time_column]);
  } catch (...) {
    return 0;
  }
}

void insert_by_time(std::vector<Row> &rows, Row row, const Layout &layout) {
  const std::int64_t when = row_time(row, layout);
  const auto where =
      std::find_if(rows.begin(), rows.end(), [&](const Row &other) {
        return row_time(other, layout) < when;
      });
  rows.insert(where, std::move(row));
}

bool move_folder(const fs::path &from, const fs::path &to, std::string &error) {
  std::error_code ec;
  if (!fs::exists(from, ec))
    return true;

  if (fs::exists(to, ec)) {

    fs::remove_all(from, ec);
    return true;
  }

  if (!to.parent_path().empty()) {
    fs::create_directories(to.parent_path(), ec);
    if (ec) {
      error =
          "Cannot create " + to.parent_path().string() + ": " + ec.message();
      return false;
    }
  }

  ec.clear();
  fs::rename(from, to, ec);
  if (ec) {
    error = "Cannot move " + from.string() + " to " + to.string() + ": " +
            ec.message();
    return false;
  }
  return true;
}

std::uintmax_t id_folders_bytes(const fs::path &dir) {
  std::uintmax_t total = 0;
  std::error_code ec;
  for (const auto &entry : fs::directory_iterator(dir, ec)) {
    if (ec)
      break;
    if (!entry.is_directory(ec))
      continue;
    if (!looks_like_id(entry.path().filename().string()))
      continue;
    total += folder_bytes(entry.path());
  }
  return total;
}

std::uintmax_t erase_id_folders(const fs::path &dir) {
  std::uintmax_t freed = 0;
  std::error_code ec;
  std::vector<fs::path> doomed;
  for (const auto &entry : fs::directory_iterator(dir, ec)) {
    if (ec)
      break;
    if (!entry.is_directory(ec))
      continue;
    if (!looks_like_id(entry.path().filename().string()))
      continue;
    doomed.push_back(entry.path());
  }

  for (const fs::path &folder : doomed) {
    freed += folder_bytes(folder);
    std::error_code removed;
    fs::remove_all(folder, removed);
  }
  return freed;
}

} // namespace store::detail
