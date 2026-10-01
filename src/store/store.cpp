#include "store/store.hpp"

#include "base/file.hpp"
#include "base/hash.hpp"
#include "compare/serialize.hpp"

#include <windows.h>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <ctime>
#include <fstream>
#include <string>
#include <system_error>
#include <utility>

namespace store {

namespace {

namespace fs = std::filesystem;

constexpr const char *kDomain = "cmpstore/1";
constexpr const char *kSingleDir = "single";
constexpr const char *kBatchDir = "batch";
constexpr const char *kIndexFile = "index.tsv";
constexpr const char *kMetaFile = "meta.txt";
constexpr const char *kManifestFile = "manifest.tsv";
constexpr const char *kResultFile = "result.cmp";
constexpr const char *kOutputFile = "output.txt";
constexpr const char *kAnswerFile = "answer.txt";

constexpr std::size_t kCopyChunk = 1u << 20;

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

std::vector<std::string> split_fields(const std::string &line) {
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

std::string join_fields(const std::vector<std::string> &fields) {
  std::string out;
  for (std::size_t i = 0; i < fields.size(); ++i) {
    if (i != 0)
      out.push_back('\t');
    out += fields[i];
  }
  return out;
}

bool read_lines(const fs::path &path, std::vector<std::string> &out,
                std::string &error) {
  out.clear();
  std::ifstream file(path, std::ios::binary);
  if (!file) {
    std::error_code ec;
    if (!fs::exists(path, ec))
      return true;
    error = "Cannot read " + path.string();
    return false;
  }

  std::string line;
  while (std::getline(file, line)) {
    if (!line.empty() && line.back() == '\r')
      line.pop_back();
    out.push_back(std::move(line));
  }
  return true;
}

bool write_file_atomic(const fs::path &path, std::string_view text,
                       std::string &error) {
  const fs::path temp = path.string() + ".tmp";
  {
    std::ofstream file(temp, std::ios::binary | std::ios::trunc);
    if (!file) {
      error = "Cannot write " + temp.string();
      return false;
    }
    file.write(text.data(), static_cast<std::streamsize>(text.size()));
    if (!file.good()) {
      error = "Cannot write " + temp.string();
      return false;
    }
  }

  std::error_code ec;
  fs::remove(path, ec);
  ec.clear();
  fs::rename(temp, path, ec);
  if (ec) {
    error = "Cannot replace " + path.string() + ": " + ec.message();
    return false;
  }
  return true;
}

std::string now_local(std::time_t when) {
  std::tm parts{};
  const std::time_t copy = when;
#if defined(_WIN32)
  localtime_s(&parts, &copy);
#else
  parts = *std::localtime(&copy);
#endif
  char buffer[64] = {};
  if (std::strftime(buffer, sizeof(buffer), "%Y-%m-%dT%H:%M:%S%z", &parts) == 0)
    return {};
  return buffer;
}

bool file_size_of(const fs::path &path, std::uint64_t &size,
                  std::string &error) {
  std::error_code ec;
  const std::uintmax_t bytes = fs::file_size(path, ec);
  if (ec) {
    error = "Cannot read " + path.string();
    return false;
  }
  size = static_cast<std::uint64_t>(bytes);
  return true;
}

bool hash_file(hash::Sha256 &hasher, const fs::path &path, std::uint64_t size,
               std::string &error) {
  std::ifstream file(path, std::ios::binary);
  if (!file) {
    error = "Cannot read " + path.string();
    return false;
  }

  std::string chunk;
  chunk.resize(kCopyChunk);
  std::uint64_t left = size;
  while (left > 0) {
    const std::size_t want =
        static_cast<std::size_t>(std::min<std::uint64_t>(left, chunk.size()));
    file.read(chunk.data(), static_cast<std::streamsize>(want));
    const std::streamsize got = file.gcount();
    if (got <= 0) {
      error = "Short read from " + path.string();
      return false;
    }
    hasher.write(chunk.data(), static_cast<std::size_t>(got));
    left -= static_cast<std::uint64_t>(got);
  }
  return true;
}

bool payload_id(const CompareResult &result, const fs::path &output_path,
                const fs::path &answer_path, std::string &id,
                std::string &result_bytes, std::string &error) {
  result_bytes = serialize_compare_result(result);

  std::uint64_t output_size = 0;
  std::uint64_t answer_size = 0;
  if (!file_size_of(output_path, output_size, error) ||
      !file_size_of(answer_path, answer_size, error))
    return false;

  hash::Sha256 hasher;
  if (!hasher.ok()) {
    error = "Cannot start a SHA-256 hash";
    return false;
  }
  hasher.write(kDomain);
  hasher.write_u64(3);

  const auto feed_start = [&hasher](const char *name, std::uint64_t size) {
    hasher.write_u64(std::char_traits<char>::length(name));
    hasher.write(name);
    hasher.write_u64(size);
  };

  feed_start(kResultFile, result_bytes.size());
  hasher.write(result_bytes);
  feed_start(kOutputFile, output_size);
  if (!hash_file(hasher, output_path, output_size, error))
    return false;
  feed_start(kAnswerFile, answer_size);
  if (!hash_file(hasher, answer_path, answer_size, error))
    return false;

  const std::string full = hasher.hex();
  if (full.size() < kIdLength) {
    error = "Cannot compute a SHA-256 hash";
    return false;
  }
  id = full.substr(0, kIdLength);
  return true;
}

bool copy_file(const fs::path &from, const fs::path &to, std::string &error) {
  const file::ReadResult read = file::read_all(from);
  if (!read) {
    error = read.message;
    return false;
  }
  const file::WriteResult written = file::write_all(to, read.data.view());
  if (!written) {
    error = written.message;
    return false;
  }
  return true;
}

class TempFolder {
public:
  bool create(const fs::path &parent, std::string &error) {
    std::error_code ec;
    fs::create_directories(parent, ec);
    if (ec) {
      error = "Cannot create " + parent.string() + ": " + ec.message();
      return false;
    }

    static unsigned counter = 0;
    for (int attempt = 0; attempt < 100; ++attempt) {
      const fs::path candidate =
          parent / (".tmp-" + std::to_string(::GetCurrentProcessId()) + "-" +
                    std::to_string(++counter));
      ec.clear();
      if (fs::create_directory(candidate, ec) && !ec) {
        path_ = candidate;
        return true;
      }
    }
    error = "Cannot create a temporary folder under " + parent.string();
    return false;
  }

  const fs::path &path() const noexcept { return path_; }
  void keep() noexcept { keep_ = true; }

  ~TempFolder() {
    if (!keep_ && !path_.empty()) {
      std::error_code ec;
      fs::remove_all(path_, ec);
    }
  }

private:
  fs::path path_;
  bool keep_{false};
};

bool move_into_place(const fs::path &temp, const fs::path &target,
                     std::string &error) {
  std::error_code ec;
  if (fs::is_directory(target, ec)) {
    fs::remove_all(temp, ec);
    return true;
  }
  ec.clear();
  fs::rename(temp, target, ec);
  if (ec) {
    error = "Cannot publish " + target.string() + ": " + ec.message();
    return false;
  }
  return true;
}

bool looks_like_id(const std::string &name) {
  if (name.size() != kIdLength)
    return false;
  for (char c : name) {
    const bool hex = (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
    if (!hex)
      return false;
  }
  return true;
}

std::uintmax_t folder_bytes(const fs::path &folder) {
  std::uintmax_t total = 0;
  std::error_code ec;
  for (const auto &entry : fs::recursive_directory_iterator(folder, ec)) {
    if (ec)
      break;
    if (!entry.is_regular_file(ec))
      continue;
    const std::uintmax_t size = entry.file_size(ec);
    if (!ec)
      total += size;
  }
  return total;
}

using Row = std::vector<std::string>;

bool read_index(const fs::path &dir, std::size_t columns,
                std::vector<Row> &rows, std::string &error) {
  std::vector<std::string> lines;
  if (!read_lines(dir / kIndexFile, lines, error))
    return false;

  rows.clear();
  for (const std::string &line : lines) {
    if (line.empty() || line[0] == '#')
      continue;
    Row fields = split_fields(line);
    if (fields.size() != columns) {
      std::fprintf(stderr, "[store] skipping an unreadable index line\n");
      continue;
    }
    rows.push_back(std::move(fields));
  }
  return true;
}

bool write_index(const fs::path &dir, const std::vector<Row> &rows,
                 const char *columns, std::string &error) {
  std::error_code ec;
  fs::create_directories(dir, ec);
  if (ec) {
    error = "Cannot create " + dir.string() + ": " + ec.message();
    return false;
  }

  std::string text = "# ";
  text += columns;
  text.push_back('\n');
  for (const Row &row : rows)
    text += join_fields(row) + "\n";

  return write_file_atomic(dir / kIndexFile, text, error);
}

void put_front(std::vector<Row> &rows, Row row, std::size_t id_column,
               std::size_t time_column, bool refresh_time_only) {
  const std::string id = row[id_column];
  auto found = std::find_if(
      rows.begin(), rows.end(), [&id, id_column](const Row &other) {
        return other.size() > id_column && other[id_column] == id;
      });

  if (found != rows.end()) {
    Row updated = *found;
    if (refresh_time_only) {
      updated[time_column] = row[time_column];
      updated[time_column + 1] = row[time_column + 1];
    } else {
      updated = std::move(row);
    }
    rows.erase(found);
    rows.insert(rows.begin(), std::move(updated));
    return;
  }

  rows.insert(rows.begin(), std::move(row));
}

constexpr const char *kSingleColumns =
    "name\ttime\tlocal\tid\tstatus\tunmatched\toutput_lines\texpect_lines";
constexpr const char *kBatchColumns =
    "time\tlocal\tid\tcases\tmatched\tdiffer\tunusable";

bool to_size(const std::string &text, std::size_t &out) {
  try {
    out = static_cast<std::size_t>(std::stoull(text));
  } catch (...) {
    return false;
  }
  return true;
}

bool parse_entry(const Row &row, Entry &out) {
  long long time = 0;
  try {
    time = std::stoll(row[1]);
  } catch (...) {
    return false;
  }
  out.name = row[0];
  out.time = time;
  out.local_time = row[2];
  out.id = row[3];
  out.status = row[4];
  return to_size(row[5], out.unmatched) && to_size(row[6], out.output_lines) &&
         to_size(row[7], out.expect_lines);
}

Row row_of(const Entry &entry) {
  return Row{escape_field(entry.name),
             std::to_string(entry.time),
             escape_field(entry.local_time),
             entry.id,
             entry.status,
             std::to_string(entry.unmatched),
             std::to_string(entry.output_lines),
             std::to_string(entry.expect_lines)};
}

bool parse_batch_entry(const Row &row, BatchEntry &out) {
  long long time = 0;
  try {
    time = std::stoll(row[0]);
  } catch (...) {
    return false;
  }
  out.time = time;
  out.local_time = row[1];
  out.id = row[2];
  return to_size(row[3], out.cases) && to_size(row[4], out.matched) &&
         to_size(row[5], out.differ) && to_size(row[6], out.unusable);
}

Row row_of(const BatchEntry &entry) {
  return Row{std::to_string(entry.time),
             escape_field(entry.local_time),
             entry.id,
             std::to_string(entry.cases),
             std::to_string(entry.matched),
             std::to_string(entry.differ),
             std::to_string(entry.unusable)};
}

std::string meta_text(const Entry &entry, std::uint64_t result_bytes,
                      std::uint64_t output_bytes, std::uint64_t answer_bytes,
                      const fs::path &output_path,
                      const fs::path &answer_path) {
  std::string text = "# comparison archive; the id covers result.cmp, "
                     "output.txt and answer.txt, not this file\n";
  const auto line = [&text](const char *key, const std::string &value) {
    text += key;
    text += '\t';
    text += escape_field(value);
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

std::size_t count_state(const std::vector<BatchItem> &items, CaseState state) {
  return static_cast<std::size_t>(
      std::count_if(items.begin(), items.end(), [state](const BatchItem &item) {
        return item.item.state == state;
      }));
}

PruneResult remove_unreferenced(const fs::path &dir,
                                const std::vector<std::string> &keep) {
  PruneResult out;
  out.success = true;

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
  if (!payload_id(*request.result, request.output_path, request.answer_path, id,
                  result_bytes, error)) {
    out.message = error;
    return out;
  }

  const fs::path dir = root / kSingleDir;
  const std::time_t now = std::time(nullptr);

  std::vector<Row> rows;
  if (!read_index(dir, 8, rows, error)) {
    out.message = error;
    return out;
  }

  Entry entry;
  entry.name = request.name;
  entry.id = id;
  entry.time = static_cast<std::int64_t>(now);
  entry.local_time = now_local(now);
  entry.status = request.result->exact_match ? "matched" : "differ";
  entry.unmatched = request.result->unmatched_line_count;
  entry.output_lines = request.result->output_line_count;
  entry.expect_lines = request.result->expect_line_count;

  const fs::path folder = dir / id;
  std::error_code ec;
  if (fs::is_directory(folder, ec)) {
    out.reused = true;
  } else {
    TempFolder temp;
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
    if (!copy_file(request.output_path, temp.path() / kOutputFile, error) ||
        !copy_file(request.answer_path, temp.path() / kAnswerFile, error)) {
      out.message = error;
      return out;
    }

    std::uint64_t output_bytes = 0;
    std::uint64_t answer_bytes = 0;
    if (!file_size_of(request.output_path, output_bytes, error) ||
        !file_size_of(request.answer_path, answer_bytes, error)) {
      out.message = error;
      return out;
    }

    const std::string meta =
        meta_text(entry, result_bytes.size(), output_bytes, answer_bytes,
                  request.output_path, request.answer_path);
    if (!write_file_atomic(temp.path() / kMetaFile, meta, error)) {
      out.message = error;
      return out;
    }

    if (!move_into_place(temp.path(), folder, error)) {
      out.message = error;
      return out;
    }
    temp.keep();
  }

  put_front(rows, row_of(entry), 3, 1, out.reused);
  if (!write_index(dir, rows, kSingleColumns, error)) {
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
  if (!read_index(root / kSingleDir, 8, rows, error)) {
    out.message = error;
    return out;
  }

  out.entries.reserve(rows.size());
  for (const Row &row : rows) {
    Entry entry;
    if (!parse_entry(row, entry))
      continue;
    out.entries.push_back(std::move(entry));
  }

  out.success = true;
  return out;
}

LoadSingleResult load_single(const fs::path &root, const std::string &id) {
  LoadSingleResult out;

  const ListResult listed = list_single(root);
  if (!listed) {
    out.message = listed.message;
    return out;
  }
  if (listed.entries.empty()) {
    out.message =
        "There is no saved comparison in " + (root / kSingleDir).string();
    return out;
  }

  const Entry *found = &listed.entries.front();
  if (!id.empty()) {
    found = nullptr;
    for (const Entry &entry : listed.entries) {
      if (entry.id == id) {
        found = &entry;
        break;
      }
    }
    if (found == nullptr) {
      out.message = "No saved comparison with id " + id;
      return out;
    }
  }

  const fs::path folder = root / kSingleDir / found->id;
  const LoadResult loaded = load_compare_result(folder / kResultFile);
  if (!loaded) {
    out.message = loaded.message;
    return out;
  }

  out.entry = *found;
  out.result = loaded.result;
  out.output_path = folder / kOutputFile;
  out.answer_path = folder / kAnswerFile;
  out.success = true;
  return out;
}

BatchSaveOutcome save_batch(const fs::path &root, const BatchRequest &request) {
  BatchSaveOutcome out;

  if (request.items.empty()) {
    out.message = "Nothing to save: the run has no cases";
    return out;
  }

  std::string error;
  const fs::path dir = root / kBatchDir;

  std::vector<std::string> case_ids(request.items.size());
  for (std::size_t i = 0; i < request.items.size(); ++i) {
    const BatchItem &item = request.items[i];
    const bool has_result = item.item.state == CaseState::Identical ||
                            item.item.state == CaseState::Differ;
    if (!has_result)
      continue;
    std::string result_bytes;
    if (!payload_id(item.item.result, item.output_path, item.answer_path,
                    case_ids[i], result_bytes, error)) {
      out.message = item.item.name + ": " + error;
      return out;
    }
  }

  hash::Sha256 run_hasher;
  if (!run_hasher.ok()) {
    out.message = "Cannot start a SHA-256 hash";
    return out;
  }
  run_hasher.write(kDomain);
  run_hasher.write_u64(request.items.size());
  for (std::size_t i = 0; i < request.items.size(); ++i) {
    run_hasher.write_u64(request.items[i].item.name.size());
    run_hasher.write(request.items[i].item.name);
    run_hasher.write_u64(case_ids[i].size());
    run_hasher.write(case_ids[i]);
  }
  const std::string full = run_hasher.hex();
  if (full.size() < kIdLength) {
    out.message = "Cannot compute a SHA-256 hash";
    return out;
  }
  const std::string id = full.substr(0, kIdLength);

  const fs::path folder = dir / id;
  std::error_code ec;
  if (fs::is_directory(folder, ec)) {
    out.reused = true;
  } else {
    TempFolder temp;
    if (!temp.create(dir, error)) {
      out.message = error;
      return out;
    }

    std::string manifest = "# name\tstate\tunmatched\tcase_id\tnote\n";
    for (std::size_t i = 0; i < request.items.size(); ++i) {
      const BatchItem &item = request.items[i];
      const bool has_result = item.item.state == CaseState::Identical ||
                              item.item.state == CaseState::Differ;

      const Row fields{
          escape_field(item.item.name), case_state_token(item.item.state),
          std::to_string(has_result ? item.item.result.unmatched_line_count
                                    : 0),
          case_ids[i], escape_field(item.item.note)};

      if (has_result) {
        const fs::path case_dir = temp.path() / item.item.name;
        fs::create_directories(case_dir, ec);
        if (ec) {
          out.message =
              "Cannot create " + case_dir.string() + ": " + ec.message();
          return out;
        }

        const file::WriteResult wrote = file::write_all(
            case_dir / kResultFile, serialize_compare_result(item.item.result));
        if (!wrote) {
          out.message = wrote.message;
          return out;
        }
        if (!item.output_path.empty() &&
            !copy_file(item.output_path, case_dir / kOutputFile, error)) {
          out.message = item.item.name + ": " + error;
          return out;
        }
        if (!item.answer_path.empty() &&
            !copy_file(item.answer_path, case_dir / kAnswerFile, error)) {
          out.message = item.item.name + ": " + error;
          return out;
        }
        ++out.archived;
      }

      manifest += join_fields(fields) + "\n";
    }

    if (!write_file_atomic(temp.path() / kManifestFile, manifest, error)) {
      out.message = error;
      return out;
    }
    if (!move_into_place(temp.path(), folder, error)) {
      out.message = error;
      return out;
    }
    temp.keep();
  }

  BatchEntry entry;
  entry.id = id;
  entry.time = static_cast<std::int64_t>(std::time(nullptr));
  entry.local_time = now_local(static_cast<std::time_t>(entry.time));
  entry.cases = request.items.size();
  entry.matched = count_state(request.items, CaseState::Identical);
  entry.differ = count_state(request.items, CaseState::Differ);
  entry.unusable = entry.cases - entry.matched - entry.differ;

  std::vector<Row> rows;
  if (!read_index(dir, 7, rows, error)) {
    out.message = error;
    return out;
  }
  put_front(rows, row_of(entry), 2, 0, out.reused);
  if (!write_index(dir, rows, kBatchColumns, error)) {
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
  if (!read_index(root / kBatchDir, 7, rows, error)) {
    out.message = error;
    return out;
  }

  out.entries.reserve(rows.size());
  for (const Row &row : rows) {
    BatchEntry entry;
    if (!parse_batch_entry(row, entry))
      continue;
    out.entries.push_back(std::move(entry));
  }

  out.success = true;
  return out;
}

BatchLoadResult load_batch(const fs::path &root, const std::string &id) {
  BatchLoadResult out;

  const BatchListResult listed = list_batch(root);
  if (!listed) {
    out.message = listed.message;
    return out;
  }
  if (listed.entries.empty()) {
    out.message = "There is no saved run in " + (root / kBatchDir).string();
    return out;
  }

  const BatchEntry *found = &listed.entries.front();
  if (!id.empty()) {
    found = nullptr;
    for (const BatchEntry &entry : listed.entries) {
      if (entry.id == id) {
        found = &entry;
        break;
      }
    }
    if (found == nullptr) {
      out.message = "No saved run with id " + id;
      return out;
    }
  }

  const fs::path folder = root / kBatchDir / found->id;
  std::vector<std::string> lines;
  std::string error;
  if (!read_lines(folder / kManifestFile, lines, error)) {
    out.message = error;
    return out;
  }

  for (const std::string &line : lines) {
    if (line.empty() || line[0] == '#')
      continue;
    const Row fields = split_fields(line);
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

    if (item.state == CaseState::Identical || item.state == CaseState::Differ) {
      const LoadResult loaded =
          load_compare_result(folder / item.name / kResultFile);
      if (!loaded) {
        item.state = CaseState::Unreadable;
        item.note = loaded.message;
      } else {
        item.result = loaded.result;
      }
    }

    out.cases.push_back(std::move(item));
  }

  out.entry = *found;
  out.success = true;
  return out;
}

namespace {

PruneResult prune_common(const fs::path &dir, const std::vector<Row> &rows,
                         std::size_t keep, const char *columns,
                         std::size_t id_column) {
  PruneResult out;
  out.success = true;

  std::vector<Row> survivors;
  survivors.reserve(std::min(keep, rows.size()));
  for (std::size_t i = 0; i < rows.size() && survivors.size() < keep; ++i)
    survivors.push_back(rows[i]);

  std::vector<std::string> keep_ids;
  keep_ids.reserve(survivors.size());
  for (const Row &row : survivors)
    keep_ids.push_back(row[id_column]);

  out.removed_entries = rows.size() - survivors.size();

  std::string error;
  if (!write_index(dir, survivors, columns, error)) {
    out.message = error;
    out.success = false;
    return out;
  }

  const PruneResult removed = remove_unreferenced(dir, keep_ids);
  out.removed_folders = removed.removed_folders;
  out.freed_bytes = removed.freed_bytes;
  return out;
}

} // namespace

PruneResult prune_single(const fs::path &root, std::size_t keep) {
  PruneResult out;

  std::string error;
  std::vector<Row> rows;
  const fs::path dir = root / kSingleDir;
  if (!read_index(dir, 8, rows, error)) {
    out.message = error;
    return out;
  }
  return prune_common(dir, rows, keep, kSingleColumns, 3);
}

PruneResult prune_batch(const fs::path &root, std::size_t keep) {
  PruneResult out;

  std::string error;
  std::vector<Row> rows;
  const fs::path dir = root / kBatchDir;
  if (!read_index(dir, 7, rows, error)) {
    out.message = error;
    return out;
  }
  return prune_common(dir, rows, keep, kBatchColumns, 2);
}

} // namespace store
