#include "store/store.hpp"

#include "base/file.hpp"
#include "compare/serialize.hpp"
#include "store/detail/archive.hpp"
#include "store/detail/fields.hpp"
#include "store/detail/files.hpp"
#include "store/detail/index.hpp"
#include "store/detail/layouts.hpp"

#include <cstdint>
#include <ctime>
#include <filesystem>
#include <string>
#include <utility>
#include <vector>
namespace coding {

namespace store {

namespace {

namespace fs = std::filesystem;
using detail::kAnswerFile;
using detail::kMetaFile;
using detail::kOutputFile;
using detail::kResultFile;
using detail::Layout;
using detail::Row;

constexpr const char *kDir = detail::kSingleDir;
const Layout kLayout = detail::kSingleLayout;

Row row_of(const Entry &entry) {
  return Row{entry.name,
             std::to_string(entry.time),
             entry.local_time,
             entry.id,
             entry.status,
             std::to_string(entry.unmatched),
             std::to_string(entry.output_lines),
             std::to_string(entry.expect_lines)};
}

bool parse_row(const Row &row, Entry &out) {
  long long time = 0;
  try {
    time = std::stoll(row[1]);
    out.unmatched = static_cast<std::size_t>(std::stoull(row[5]));
    out.output_lines = static_cast<std::size_t>(std::stoull(row[6]));
    out.expect_lines = static_cast<std::size_t>(std::stoull(row[7]));
  } catch (...) {
    return false;
  }

  out.name = row[0];
  out.time = time;
  out.local_time = row[2];
  out.id = row[3];
  out.status = row[4];
  return true;
}

std::string meta_text(const Entry &entry, std::uint64_t result_bytes,
                      std::uint64_t output_bytes, std::uint64_t answer_bytes,
                      const fs::path &output_path,
                      const fs::path &answer_path) {
  std::string text =
      "# comparison archive; the id covers result.cmp, output.txt and "
      "answer.txt, not this file\n";

  const auto line = [&text](const char *key, const std::string &value) {
    text += detail::join_row({key, value});
    text += '\n';
  };
  line("id", entry.id);
  line("version", "1");
  line("status", entry.status);
  line("first_saved", std::to_string(entry.time));
  line("first_saved_local", entry.local_time);
  line("first_name", entry.name);
  line("unmatched", std::to_string(entry.unmatched));
  line("output_lines", std::to_string(entry.output_lines));
  line("expect_lines", std::to_string(entry.expect_lines));
  line("result_bytes", std::to_string(result_bytes));
  line("output_bytes", std::to_string(output_bytes));
  line("answer_bytes", std::to_string(answer_bytes));
  line("output_source", output_path.string());
  line("answer_source", answer_path.string());
  return text;
}

} // namespace

SaveOutcome save_single(const fs::path &root, const SaveRequest &request) {
  SaveOutcome out;

  if (request.result == nullptr) {
    out.message = "Nothing to save: there is no comparison result";
    return out;
  }
  if (request.name.empty()) {
    out.message = "Nothing to save: an archived comparison needs a name";
    return out;
  }

  std::string error;
  std::string id;
  std::string result_bytes;
  if (!detail::payload_id(*request.result, request.output_path,
                          request.answer_path, id, result_bytes, error)) {
    out.message = error;
    return out;
  }

  const fs::path dir = root / kDir;
  const std::time_t now = std::time(nullptr);

  std::vector<Row> rows;
  if (!detail::read(dir, kLayout, rows, error)) {
    out.message = error;
    return out;
  }

  Entry entry;
  entry.name = request.name;
  entry.id = id;
  entry.time = static_cast<std::int64_t>(now);
  entry.local_time = detail::local_time(now);
  entry.status = request.result->exact_match ? "matched" : "differ";
  entry.unmatched = request.result->unmatched_line_count;
  entry.output_lines = request.result->output_line_count;
  entry.expect_lines = request.result->expect_line_count;

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

    const file::WriteResult wrote =
        file::write_all(temp.path() / kResultFile, result_bytes);
    if (!wrote) {
      out.message = wrote.message;
      return out;
    }
    if (!detail::copy_file(request.output_path, temp.path() / kOutputFile,
                           error) ||
        !detail::copy_file(request.answer_path, temp.path() / kAnswerFile,
                           error)) {
      out.message = error;
      return out;
    }

    std::uint64_t output_bytes = 0;
    std::uint64_t answer_bytes = 0;
    if (!detail::file_size_of(request.output_path, output_bytes, error) ||
        !detail::file_size_of(request.answer_path, answer_bytes, error)) {
      out.message = error;
      return out;
    }

    const std::string meta =
        meta_text(entry, result_bytes.size(), output_bytes, answer_bytes,
                  request.output_path, request.answer_path);
    if (!detail::write_file_atomic(temp.path() / kMetaFile, meta, error)) {
      out.message = error;
      return out;
    }

    if (!detail::publish(temp.path(), folder, error)) {
      out.message = error;
      return out;
    }
    temp.keep();
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

ListResult list_single(const fs::path &root) {
  ListResult out;

  std::string error;
  std::vector<Row> rows;
  if (!detail::read(root / kDir, kLayout, rows, error)) {
    out.message = error;
    return out;
  }

  out.entries.reserve(rows.size());
  for (const Row &row : rows) {
    Entry entry;
    if (!parse_row(row, entry))
      continue;
    out.entries.push_back(std::move(entry));
  }

  out.success = true;
  return out;
}

LoadSingleResult load_single(const fs::path &root, const std::string &id) {
  LoadSingleResult out;

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

  Entry entry;
  if (!parse_row(*chosen, entry)) {
    out.message =
        "The index line for " + (*chosen)[kLayout.id_column] + " is unreadable";
    return out;
  }

  const fs::path folder = dir / entry.id;
  const LoadResult loaded = load_compare_result(folder / kResultFile);
  if (!loaded) {
    out.message = loaded.message;
    return out;
  }

  out.entry = std::move(entry);
  out.result = loaded.result;
  out.output_path = folder / kOutputFile;
  out.answer_path = folder / kAnswerFile;
  out.success = true;
  return out;
}

PruneResult prune_single(const fs::path &root, std::size_t keep) {
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
} // namespace coding
