#include "store/detail/archive.hpp"

#include "base/file.hpp"
#include "compare/serialize.hpp"
#include "store/detail/files.hpp"
#include "store/store.hpp"

#include <windows.h>

#include <algorithm>
#include <cstdint>
#include <fstream>
#include <string>
#include <string_view>
#include <system_error>

namespace store::detail {

namespace fs = std::filesystem;

namespace {

constexpr const char *kDomain = "cmpstore/1";
constexpr const char *kResultFile = "result.cmp";
constexpr const char *kOutputFile = "output.txt";
constexpr const char *kAnswerFile = "answer.txt";
constexpr std::size_t kCopyChunk = 1u << 20;

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

} // namespace

bool finish_id(hash::Sha256 &hasher, std::string &id, std::string &error) {
  const std::string full = hasher.hex();
  if (full.size() < store::kIdLength) {
    error = "Cannot compute a SHA-256 hash";
    return false;
  }
  id = full.substr(0, store::kIdLength);
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

  return finish_id(hasher, id, error);
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

bool TempFolder::create(const fs::path &parent, std::string &error) {
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

TempFolder::~TempFolder() {
  if (!keep_ && !path_.empty()) {
    std::error_code ec;
    fs::remove_all(path_, ec);
  }
}

bool publish(const fs::path &temp, const fs::path &target, std::string &error) {
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

bool looks_like_id(std::string_view name) noexcept {
  if (name.size() != store::kIdLength)
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

} // namespace store::detail
