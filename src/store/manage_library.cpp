#include "store/store.hpp"

#include "store/detail/archive.hpp"
#include "store/detail/fields.hpp"
#include "store/detail/files.hpp"
#include "store/detail/index.hpp"
#include "store/detail/layouts.hpp"
#include "store/detail/manage.hpp"

#include <algorithm>
#include <cstdint>
#include <ctime>
#include <filesystem>
#include <string>
#include <system_error>
#include <utility>
#include <vector>
namespace coding {

namespace store {

namespace {

namespace fs = std::filesystem;
using detail::Layout;
using detail::Row;

struct Spec {
  const char *store_dir;
  Layout store;
  Layout trash;
  std::size_t trashed;
  void (*fill)(const Row &row, TrashEntry &out);
};

void fill_single(const Row &row, TrashEntry &out) {
  out.name = row[0];
  out.local_time = row[2];
  out.id = row[3];
  out.status = row[4];
  out.count = 1;
  try {
    out.unmatched = static_cast<std::size_t>(std::stoull(row[5]));
  } catch (...) {
    out.unmatched = 0;
  }
}

void fill_batch(const Row &row, TrashEntry &out) {
  out.name = row[2];
  out.local_time = row[1];
  out.id = row[2];
  out.status = "run";
  try {
    out.count = static_cast<std::size_t>(std::stoull(row[3]));
    out.unmatched = static_cast<std::size_t>(std::stoull(row[5]));
  } catch (...) {
    out.count = 0;
    out.unmatched = 0;
  }
}

constexpr Spec kSingleSpec{detail::kSingleDir, detail::kSingleLayout,
                           detail::kSingleTrashLayout,
                           detail::kSingleTrashedColumn, fill_single};
constexpr Spec kBatchSpec{detail::kBatchDir, detail::kBatchLayout,
                          detail::kBatchTrashLayout,
                          detail::kBatchTrashedColumn, fill_batch};

fs::path store_index_dir(const fs::path &root, const Spec &spec) {
  return root / spec.store_dir;
}

fs::path trash_index_dir(const fs::path &root, const Spec &spec) {
  return root / detail::kTrashDir / spec.store_dir;
}

fs::path pin_path(const fs::path &root, const Spec &spec) {
  return root / spec.store_dir / detail::kPinFile;
}

std::vector<PinEntry> read_pins(const fs::path &path, std::string &error) {
  std::vector<PinEntry> pins;

  std::vector<std::string> lines;
  if (!detail::read_lines(path, lines, error))
    return pins;

  for (const std::string &line : lines) {
    if (line.empty() || line[0] == '#')
      continue;
    const Row fields = detail::split_row(line);
    if (fields.size() != 2)
      continue;
    pins.push_back(PinEntry{fields[0], fields[1]});
  }
  return pins;
}

bool write_pins(const fs::path &path, const std::vector<PinEntry> &pins,
                std::string &error) {
  std::string text = "# id\tname\n";
  for (const PinEntry &pin : pins)
    text += detail::join_row({pin.id, pin.name}) + "\n";
  return detail::write_file_atomic(path, text, error);
}

ResultBase set_pin(const fs::path &root, const Spec &spec,
                   const std::string &id, bool on, const char *what) {
  ResultBase out;

  std::string error;
  const fs::path path = pin_path(root, spec);
  std::vector<PinEntry> pins = read_pins(path, error);
  if (!error.empty()) {
    out.message = error;
    return out;
  }

  const auto found =
      std::find_if(pins.begin(), pins.end(),
                   [&id](const PinEntry &pin) { return pin.id == id; });

  if (on) {
    if (found != pins.end()) {
      out.success = true;
      return out;
    }

    std::string label = id;
    std::vector<Row> rows;
    if (detail::read(store_index_dir(root, spec), spec.store, rows, error)) {
      const auto row = std::find_if(
          rows.begin(), rows.end(), [&id, &spec](const Row &candidate) {
            return candidate[spec.store.id_column] == id;
          });
      if (row != rows.end())
        label = (*row)[0];
    }
    if (!error.empty()) {
      out.message = error;
      return out;
    }

    pins.insert(pins.begin(), PinEntry{id, label});
  } else {
    if (found == pins.end()) {
      out.success = true;
      return out;
    }
    pins.erase(found);
  }

  if (!write_pins(path, pins, error)) {
    out.message = error;
    return out;
  }

  out.success = true;
  out.message = std::string("pin ") + what;
  return out;
}

PinListResult list_pins(const fs::path &root, const Spec &spec) {
  PinListResult out;
  std::string error;
  out.entries = read_pins(pin_path(root, spec), error);
  if (!error.empty()) {
    out.message = error;
    return out;
  }
  out.success = true;
  return out;
}

bool is_pinned(const std::vector<PinEntry> &pins, const std::string &id) {
  return std::any_of(pins.begin(), pins.end(),
                     [&id](const PinEntry &pin) { return pin.id == id; });
}

TrashMoveResult trash_one(const fs::path &root, const Spec &spec,
                          const std::string &id) {
  TrashMoveResult out;

  std::string error;
  Row row;
  if (!detail::take_row(store_index_dir(root, spec), spec.store, id, row,
                        error)) {
    out.message = error;
    return out;
  }

  out.name = row[0];

  const std::time_t now = std::time(nullptr);
  row.push_back(std::to_string(static_cast<std::int64_t>(now)));
  if (spec.trash.columns_count != row.size()) {
    out.message = "Internal error: the trash row has the wrong width";
    return out;
  }

  std::vector<Row> rows;
  if (!detail::read(trash_index_dir(root, spec), spec.trash, rows, error)) {
    out.message = error;
    return out;
  }
  rows.insert(rows.begin(), std::move(row));
  if (!detail::write(trash_index_dir(root, spec), spec.trash, rows, error)) {
    out.message = error;
    return out;
  }

  const fs::path from = store_index_dir(root, spec) / id;
  const fs::path to = trash_index_dir(root, spec) / id;
  if (!detail::move_folder(from, to, error)) {
    out.message = error;
    return out;
  }

  const ResultBase unpinned = set_pin(root, spec, id, false, "unpin");
  if (!unpinned) {
    out.message = unpinned.message;
    return out;
  }

  out.success = true;
  return out;
}

ResultBase restore_one(const fs::path &root, const Spec &spec,
                       const std::string &id) {
  ResultBase out;

  std::string error;
  Row row;
  if (!detail::take_row(trash_index_dir(root, spec), spec.trash, id, row,
                        error)) {
    out.message = error;
    return out;
  }
  row.pop_back();

  std::vector<Row> rows;
  if (!detail::read(store_index_dir(root, spec), spec.store, rows, error)) {
    out.message = error;
    return out;
  }
  detail::insert_by_time(rows, std::move(row), spec.store);
  if (!detail::write(store_index_dir(root, spec), spec.store, rows, error)) {
    out.message = error;
    return out;
  }

  const fs::path from = trash_index_dir(root, spec) / id;
  const fs::path to = store_index_dir(root, spec) / id;
  if (!detail::move_folder(from, to, error)) {
    out.message = error;
    return out;
  }

  out.success = true;
  return out;
}

EraseResult erase_one(const fs::path &root, const Spec &spec,
                      const std::string &id) {
  EraseResult out;

  std::string error;
  Row row;
  if (!detail::take_row(trash_index_dir(root, spec), spec.trash, id, row,
                        error)) {
    out.message = error;
    return out;
  }

  const fs::path folder = trash_index_dir(root, spec) / id;
  std::error_code ec;
  if (fs::is_directory(folder, ec)) {
    detail::folder_bytes(folder);
    fs::remove_all(folder, ec);
  }
  out.removed = 1;
  out.success = true;
  return out;
}

EraseResult empty_one(const fs::path &root, const Spec &spec) {
  EraseResult out;

  std::string error;
  std::vector<Row> rows;
  const fs::path dir = trash_index_dir(root, spec);
  if (!detail::read(dir, spec.trash, rows, error)) {
    out.message = error;
    return out;
  }

  out.removed = rows.size();
  out.freed_bytes = detail::erase_id_folders(dir);

  if (!detail::write(dir, spec.trash, {}, error)) {
    out.message = error;
    return out;
  }
  out.success = true;
  return out;
}

TrashLimitResult limit_trash(const fs::path &root, const Spec &spec,
                             std::uintmax_t max_bytes) {
  TrashLimitResult out;

  const fs::path dir = trash_index_dir(root, spec);
  std::string error;
  std::vector<Row> rows;
  if (!detail::read(dir, spec.trash, rows, error)) {
    out.message = error;
    return out;
  }

  out.bytes = detail::id_folders_bytes(dir);
  if (max_bytes == 0) {
    out.success = true;
    return out;
  }

  while (!rows.empty() && out.bytes > max_bytes) {
    const Row &oldest = rows.back();
    const fs::path folder = dir / oldest[spec.trash.id_column];

    std::error_code ec;
    if (fs::is_directory(folder, ec)) {
      out.freed_bytes += detail::folder_bytes(folder);
      fs::remove_all(folder, ec);
    }
    rows.pop_back();
    ++out.removed;
    out.bytes = detail::id_folders_bytes(dir);
  }

  if (out.removed != 0 && !detail::write(dir, spec.trash, rows, error)) {
    out.message = error;
    return out;
  }

  out.success = true;
  return out;
}

TrashListResult list_trash(const fs::path &root, const Spec &spec) {
  TrashListResult out;

  const fs::path dir = trash_index_dir(root, spec);
  std::string error;
  std::vector<Row> rows;
  if (!detail::read(dir, spec.trash, rows, error)) {
    out.message = error;
    return out;
  }

  out.entries.reserve(rows.size());
  for (const Row &row : rows) {
    TrashEntry entry;
    spec.fill(row, entry);
    try {
      entry.time = std::stoll(row[spec.store.time_column]);
      entry.trashed_time = std::stoll(row[spec.trashed]);
    } catch (...) {
      continue;
    }
    entry.trashed_local =
        detail::local_time(static_cast<std::time_t>(entry.trashed_time));
    out.entries.push_back(std::move(entry));
  }

  out.bytes = detail::id_folders_bytes(dir);
  out.success = true;
  return out;
}

CountLimitResult enforce_limit(const fs::path &root, const Spec &spec,
                               std::size_t max_unprotected) {
  CountLimitResult out;
  out.success = true;

  if (max_unprotected == 0)
    return out;

  std::string error;
  std::vector<PinEntry> pins = read_pins(pin_path(root, spec), error);
  if (!error.empty()) {
    out.message = error;
    out.success = false;
    return out;
  }

  for (;;) {
    std::vector<Row> rows;
    if (!detail::read(store_index_dir(root, spec), spec.store, rows, error)) {
      out.message = error;
      out.success = false;
      return out;
    }

    const Row *victim = nullptr;
    std::size_t unprotected = 0;
    for (const Row &row : rows) {
      if (!is_pinned(pins, row[spec.store.id_column]))
        ++unprotected;
    }
    if (unprotected <= max_unprotected)
      break;

    for (auto it = rows.rbegin(); it != rows.rend(); ++it) {
      if (!is_pinned(pins, (*it)[spec.store.id_column])) {
        victim = &*it;
        break;
      }
    }
    if (victim == nullptr)
      break;

    const std::string id = (*victim)[spec.store.id_column];
    if (!trash_one(root, spec, id)) {
      out.message = "Cannot move " + id + " to the trash: " + out.message;
      out.success = false;
      return out;
    }
    ++out.moved;
  }

  return out;
}

} // namespace

PinListResult list_pins_single(const fs::path &root) {
  return list_pins(root, kSingleSpec);
}
PinListResult list_pins_batch(const fs::path &root) {
  return list_pins(root, kBatchSpec);
}
ResultBase pin_single(const fs::path &root, const std::string &id) {
  return set_pin(root, kSingleSpec, id, true, "single");
}
ResultBase unpin_single(const fs::path &root, const std::string &id) {
  return set_pin(root, kSingleSpec, id, false, "single");
}
ResultBase pin_batch(const fs::path &root, const std::string &id) {
  return set_pin(root, kBatchSpec, id, true, "batch");
}
ResultBase unpin_batch(const fs::path &root, const std::string &id) {
  return set_pin(root, kBatchSpec, id, false, "batch");
}

TrashListResult list_trash_single(const fs::path &root) {
  return list_trash(root, kSingleSpec);
}
TrashListResult list_trash_batch(const fs::path &root) {
  return list_trash(root, kBatchSpec);
}
TrashMoveResult trash_single(const fs::path &root, const std::string &id) {
  return trash_one(root, kSingleSpec, id);
}
TrashMoveResult trash_batch(const fs::path &root, const std::string &id) {
  return trash_one(root, kBatchSpec, id);
}
ResultBase restore_single(const fs::path &root, const std::string &id) {
  return restore_one(root, kSingleSpec, id);
}
ResultBase restore_batch(const fs::path &root, const std::string &id) {
  return restore_one(root, kBatchSpec, id);
}
EraseResult erase_trash_single(const fs::path &root, const std::string &id) {
  return erase_one(root, kSingleSpec, id);
}
EraseResult erase_trash_batch(const fs::path &root, const std::string &id) {
  return erase_one(root, kBatchSpec, id);
}
EraseResult empty_trash_single(const fs::path &root) {
  return empty_one(root, kSingleSpec);
}
EraseResult empty_trash_batch(const fs::path &root) {
  return empty_one(root, kBatchSpec);
}
TrashLimitResult limit_trash_single(const fs::path &root,
                                    std::uintmax_t max_bytes) {
  return limit_trash(root, kSingleSpec, max_bytes);
}
TrashLimitResult limit_trash_batch(const fs::path &root,
                                   std::uintmax_t max_bytes) {
  return limit_trash(root, kBatchSpec, max_bytes);
}

CountLimitResult enforce_single_limit(const fs::path &root,
                                      std::size_t max_unprotected) {
  return enforce_limit(root, kSingleSpec, max_unprotected);
}
CountLimitResult enforce_batch_limit(const fs::path &root,
                                     std::size_t max_unprotected) {
  return enforce_limit(root, kBatchSpec, max_unprotected);
}

} // namespace store
} // namespace coding
