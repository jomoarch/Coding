#ifndef STORE_DETAIL_INDEX_HPP
#define STORE_DETAIL_INDEX_HPP

#include "base/result.hpp"

#include <cstddef>
#include <filesystem>
#include <string>
#include <vector>

namespace store::detail {

using Row = std::vector<std::string>;

struct Layout {
  const char *columns;
  std::size_t columns_count;
  std::size_t id_column;
  std::size_t time_column;
};

bool read(const std::filesystem::path &dir, const Layout &layout,
          std::vector<Row> &rows, std::string &error);

bool write(const std::filesystem::path &dir, const Layout &layout,
           const std::vector<Row> &rows, std::string &error);

void put_front(std::vector<Row> &rows, Row row, const Layout &layout,
               bool refresh_time_only);

const Row *select(const std::vector<Row> &rows, const Layout &layout,
                  const std::string &id, std::string &error);

struct PruneOutcome {
  std::size_t removed_entries{0};
  std::size_t removed_folders{0};
  std::uintmax_t freed_bytes{0};
};

bool prune(const std::filesystem::path &dir, const Layout &layout,
           std::size_t keep, PruneOutcome &out, std::string &error);

} // namespace store::detail

#endif // STORE_DETAIL_INDEX_HPP
