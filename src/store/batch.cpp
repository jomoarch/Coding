#include "store/store.hpp"

#include "base/file.hpp"
#include "base/hash.hpp"
#include "compare/case.hpp"
#include "compare/serialize.hpp"
#include "store/detail/archive.hpp"
#include "store/detail/fields.hpp"
#include "store/detail/files.hpp"
#include "store/detail/index.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdio>
#include <ctime>
#include <filesystem>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

namespace store {

namespace {

namespace fs = std::filesystem;
using detail::Layout;
using detail::Row;

constexpr const char *kDir = "batch";
constexpr const char *kManifestFile = "manifest.tsv";
constexpr const char *kResultFile = "result.cmp";
constexpr const char *kOutputFile = "output.txt";
constexpr const char *kAnswerFile = "answer.txt";

constexpr const char *kColumns =
    "time\tlocal\tid\tcases\tmatched\tdiffer\tunusable";
const Layout kLayout{kColumns, 7, 2, 0};

constexpr const char *kManifestColumns =
    "# name\tstate\tunmatched\tcase_id\tnote\n";

bool has_result(CaseState state) noexcept {
  return state == CaseState::Identical || state == CaseState::Differ;
}

Row row_of(const BatchEntry &entry) {
  return Row{std::to_string(entry.time),
             entry.local_time,
             entry.id,
             std::to_string(entry.cases),
             std::to_string(entry.matched),
             std::to_string(entry.differ),
             std::to_string(entry.unusable)};
}

bool parse_row(const Row &row, BatchEntry &out) {
  long long time = 0;
  try {
    time = std::stoll(row[0]);
    out.cases = static_cast<std::size_t>(std::stoull(row[3]));
    out.matched = static_cast<std::size_t>(std::stoull(row[4]));
    out.differ = static_cast<std::size_t>(std::stoull(row[5]));
    out.unusable = static_cast<std::size_t>(std::stoull(row[6]));
  } catch (...) {
    return false;
  }

  out.time = time;
  out.local_time = row[1];
  out.id = row[2];
  return true;
}

bool run_id(const std::vector<BatchItem> &items,
            const std::vector<std::string> &case_ids, std::string &id,
            std::string &error) {
  hash::Sha256 hasher;
  if (!hasher.ok()) {
    error = "Cannot start a SHA-256 hash";
    return false;
  }

  hasher.write("cmpstore/1");
  hasher.write_u64(items.size());
  for (std::size_t i = 0; i < items.size(); ++i) {
    hasher.write_u64(items[i].item.name.size());
    hasher.write(items[i].item.name);
    hasher.write_u64(case_ids[i].size());
    hasher.write(case_ids[i]);
  }
  return detail::finish_id(hasher, id, error);
}

bool fill_run(const fs::path &folder, const std::vector<BatchItem> &items,
              const std::vector<std::string> &case_ids, std::size_t &archived,
              std::string &error) {
  std::string manifest = kManifestColumns;

  for (std::size_t i = 0; i < items.size(); ++i) {
    const BatchItem &item = items[i];
    const bool compared = has_result(item.item.state);

    const Row fields{
        item.item.name, case_state_token(item.item.state),
        std::to_string(compared ? item.item.result.unmatched_line_count : 0),
        case_ids[i], item.item.note};
    manifest += detail::join_row(fields) + "\n";

    if (!compared)
      continue;

    const fs::path case_dir = folder / item.item.name;
    std::error_code ec;
    fs::create_directories(case_dir, ec);
    if (ec) {
      error = "Cannot create " + case_dir.string() + ": " + ec.message();
      return false;
    }

    const file::WriteResult wrote = file::write_all(
        case_dir / kResultFile, serialize_compare_result(item.item.result));
    if (!wrote) {
      error = wrote.message;
      return false;
    }
    if (!item.output_path.empty() &&
        !detail::copy_file(item.output_path, case_dir / kOutputFile, error))
      return false;
    if (!item.answer_path.empty() &&
        !detail::copy_file(item.answer_path, case_dir / kAnswerFile, error))
      return false;

    ++archived;
  }

  return detail::write_file_atomic(folder / kManifestFile, manifest, error);
}

std::vector<Case> read_manifest(const fs::path &folder) {
  std::vector<Case> cases;

  std::vector<std::string> lines;
  std::string error;
  if (!detail::read_lines(folder / kManifestFile, lines, error))
    return cases;

  for (const std::string &line : lines) {
    if (line.empty() || line[0] == '#')
      continue;

    const Row fields = detail::split_row(line);
    if (fields.size() != 5) {
      std::fprintf(stderr, "[store] skipping an unreadable manifest line\n");
      continue;
    }

    Case item;
    item.name = fields[0];
    if (!case_state_from_token(fields[1], item.state)) {
      item.state = CaseState::Unreadable;
      item.note = "unknown state in the manifest";
    }
    item.note = fields[4];

    if (has_result(item.state)) {
      const LoadResult loaded =
          load_compare_result(folder / item.name / kResultFile);
      if (!loaded) {
        item.state = CaseState::Unreadable;
        item.note = loaded.message;
      } else {
        item.result = loaded.result;
      }
    }
    cases.push_back(std::move(item));
  }
  return cases;
}

} // namespace

BatchSaveOutcome save_batch(const fs::path &root, const BatchRequest &request) {
  BatchSaveOutcome out;

  if (request.items.empty()) {
    out.message = "Nothing to save: the run has no cases";
    return out;
  }

  std::string error;
  const fs::path dir = root / kDir;

  std::vector<std::string> case_ids(request.items.size());
  for (std::size_t i = 0; i < request.items.size(); ++i) {
    const BatchItem &item = request.items[i];
    if (!has_result(item.item.state))
      continue;

    std::string result_bytes;
    if (!detail::payload_id(item.item.result, item.output_path,
                            item.answer_path, case_ids[i], result_bytes,
                            error)) {
      out.message = item.item.name + ": " + error;
      return out;
    }
  }

  std::string id;
  if (!run_id(request.items, case_ids, id, error)) {
    out.message = error;
    return out;
  }

  const fs::path folder = dir / id;
  std::error_code ec;
  if (fs::is_directory(folder, ec)) {
    out.reused = true;
  } else {
    detail::TempFolder temp;
    if (!temp.create(dir, error)) {
      out.message = error;
      return out;
    }
    if (!fill_run(temp.path(), request.items, case_ids, out.archived, error)) {
      out.message = error;
      return out;
    }
    if (!detail::publish(temp.path(), folder, error)) {
      out.message = error;
      return out;
    }
    temp.keep();
  }

  BatchEntry entry;
  entry.id = id;
  entry.time = static_cast<std::int64_t>(std::time(nullptr));
  entry.local_time = detail::local_time(static_cast<std::time_t>(entry.time));
  entry.cases = request.items.size();
  for (const BatchItem &item : request.items) {
    if (item.item.state == CaseState::Identical)
      ++entry.matched;
    else if (item.item.state == CaseState::Differ)
      ++entry.differ;
    else
      ++entry.unusable;
  }

  std::vector<Row> rows;
  if (!detail::read(dir, kLayout, rows, error)) {
    out.message = error;
    return out;
  }
  detail::put_front(rows, row_of(entry), kLayout, out.reused);
  if (!detail::write(dir, kLayout, rows, error)) {
    out.message = error;
    return out;
  }

  out.id = id;
  out.local_time = entry.local_time;
  out.success = true;
  return out;
}

BatchListResult list_batch(const fs::path &root) {
  BatchListResult out;

  std::string error;
  std::vector<Row> rows;
  if (!detail::read(root / kDir, kLayout, rows, error)) {
    out.message = error;
    return out;
  }

  out.entries.reserve(rows.size());
  for (const Row &row : rows) {
    BatchEntry entry;
    if (!parse_row(row, entry))
      continue;
    out.entries.push_back(std::move(entry));
  }

  out.success = true;
  return out;
}

BatchLoadResult load_batch(const fs::path &root, const std::string &id) {
  BatchLoadResult out;

  const fs::path dir = root / kDir;
  std::string error;
  std::vector<Row> rows;
  if (!detail::read(dir, kLayout, rows, error)) {
    out.message = error;
    return out;
  }

  const Row *chosen = detail::select(rows, kLayout, id, error);
  if (chosen == nullptr) {
    out.message = error;
    return out;
  }

  BatchEntry entry;
  if (!parse_row(*chosen, entry)) {
    out.message =
        "The index line for " + (*chosen)[kLayout.id_column] + " is unreadable";
    return out;
  }

  const fs::path folder = dir / entry.id;
  out.cases = read_manifest(folder);
  if (out.cases.empty()) {
    out.message = "The saved run has no readable manifest: " +
                  (folder / kManifestFile).string();
    return out;
  }

  out.entry = std::move(entry);
  out.success = true;
  return out;
}

PruneResult prune_batch(const fs::path &root, std::size_t keep) {
  PruneResult out;

  detail::PruneOutcome outcome;
  std::string error;
  if (!detail::prune(root / kDir, kLayout, keep, outcome, error)) {
    out.message = error;
    return out;
  }

  out.removed_entries = outcome.removed_entries;
  out.removed_folders = outcome.removed_folders;
  out.freed_bytes = outcome.freed_bytes;
  out.success = true;
  return out;
}

} // namespace store
