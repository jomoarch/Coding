#include "store/detail/index.hpp"

#include "store/detail/archive.hpp"
#include "store/detail/fields.hpp"
#include "store/detail/files.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdio>
#include <filesystem>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

namespace store::detail {

namespace fs = std::filesystem;

namespace {

constexpr const char *kIndexFile = "index.tsv";

bool read_all_rows(const fs::path &dir, const Layout &layout,
                   std::vector<Row> &rows, std::string &error) {
  std::vector<std::string> lines;
  if (!read_lines(dir / kIndexFile, lines, error))
    return false;

  rows.clear();
  for (const std::string &line : lines) {
    if (line.empty() || line[0] == '#')
      continue;

    Row fields = split_row(line);
    if (fields.size() != layout.columns_count) {
      std::fprintf(stderr, "[store] skipping an unreadable index line\n");
      continue;
    }
    rows.push_back(std::move(fields));
  }
  return true;
}

PruneOutcome remove_unreferenced(const fs::path &dir,
                                 const std::vector<std::string> &keep) {
  PruneOutcome out;

  std::error_code ec;
  std::vector<fs::path> doomed;
  for (const auto &entry : fs::directory_iterator(dir, ec)) {
    if (ec)
      break;
    if (!entry.is_directory(ec))
      continue;

    const std::string name = entry.path().filename().string();
    if (!looks_like_id(name))
      continue;
    if (std::find(keep.begin(), keep.end(), name) != keep.end())
      continue;
    doomed.push_back(entry.path());
  }

  for (const fs::path &folder : doomed) {
    out.freed_bytes += folder_bytes(folder);
    std::error_code removed;
    fs::remove_all(folder, removed);
    if (!removed)
      ++out.removed_folders;
  }
  return out;
}

} // namespace

bool read(const fs::path &dir, const Layout &layout, std::vector<Row> &rows,
          std::string &error) {
  return read_all_rows(dir, layout, rows, error);
}

bool write(const fs::path &dir, const Layout &layout,
           const std::vector<Row> &rows, std::string &error) {
  std::error_code ec;
  fs::create_directories(dir, ec);
  if (ec) {
    error = "Cannot create " + dir.string() + ": " + ec.message();
    return false;
  }

  std::string text = "# ";
  text += layout.columns;
  text.push_back('\n');
  for (const Row &row : rows)
    text += join_row(row) + "\n";

  return write_file_atomic(dir / kIndexFile, text, error);
}

void put_front(std::vector<Row> &rows, Row row, const Layout &layout,
               bool refresh_time_only) {
  const std::string id = row[layout.id_column];
  auto found = std::find_if(rows.begin(), rows.end(), [&](const Row &other) {
    return other.size() > layout.id_column && other[layout.id_column] == id;
  });

  if (found == rows.end()) {
    rows.insert(rows.begin(), std::move(row));
    return;
  }

  Row updated = *found;
  if (refresh_time_only) {
    updated[layout.time_column] = row[layout.time_column];
    updated[layout.time_column + 1] = row[layout.time_column + 1];
  } else {
    updated = std::move(row);
  }
  rows.erase(found);
  rows.insert(rows.begin(), std::move(updated));
}

const Row *select(const std::vector<Row> &rows, const Layout &layout,
                  const std::string &id, std::string &error) {
  if (rows.empty()) {
    error = "The archive is empty";
    return nullptr;
  }
  if (id.empty())
    return &rows.front();

  for (const Row &row : rows) {
    if (row[layout.id_column] == id)
      return &row;
  }
  error = "There is no archive with id " + id;
  return nullptr;
}

bool prune(const fs::path &dir, const Layout &layout, std::size_t keep,
           PruneOutcome &out, std::string &error) {
  std::vector<Row> rows;
  if (!read_all_rows(dir, layout, rows, error))
    return false;

  std::vector<Row> survivors;
  survivors.reserve(std::min(keep, rows.size()));
  for (std::size_t i = 0; i < rows.size() && survivors.size() < keep; ++i)
    survivors.push_back(rows[i]);

  std::vector<std::string> keep_ids;
  keep_ids.reserve(survivors.size());
  for (const Row &row : survivors)
    keep_ids.push_back(row[layout.id_column]);

  out.removed_entries = rows.size() - survivors.size();

  if (!write(dir, layout, survivors, error))
    return false;

  const PruneOutcome removed = remove_unreferenced(dir, keep_ids);
  out.removed_folders = removed.removed_folders;
  out.freed_bytes = removed.freed_bytes;
  return true;
}

} // namespace store::detail
