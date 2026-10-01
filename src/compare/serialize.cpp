#include "compare/serialize.hpp"

#include "base/file.hpp"

#include "xxhash.h"

#include <cstdint>
#include <cstring>
#include <string>
#include <string_view>
#include <utility>

namespace {

constexpr char kMagic[4] = {'C', 'M', 'P', 'R'};
constexpr std::uint8_t kVersion = 1;
constexpr std::size_t kHeaderSize = 4 + 1 + 8 + 8;

constexpr std::size_t kMinLine = 8 + 1 + 1 + 8 + 8 + 8;
constexpr std::size_t kMinToken = 8 + 8 + 8;

void put_u8(std::string &out, std::uint8_t value) {
  out.push_back(static_cast<char>(value));
}

void put_u64(std::string &out, std::uint64_t value) {
  for (int i = 0; i < 8; ++i)
    out.push_back(static_cast<char>((value >> (8 * i)) & 0xFFu));
}

void put_str(std::string &out, std::string_view text) {
  put_u64(out, text.size());
  out.append(text);
}

std::uint64_t load_u64(const char *data) {
  std::uint64_t value = 0;
  for (int i = 0; i < 8; ++i)
    value |= static_cast<std::uint64_t>(static_cast<unsigned char>(data[i]))
             << (8 * i);
  return value;
}

class Reader {
public:
  Reader(const char *data, std::size_t size) : at_(data), end_(data + size) {}

  bool ok() const noexcept { return good_; }

  std::size_t left() const noexcept {
    return static_cast<std::size_t>(end_ - at_);
  }

  std::uint8_t u8() {
    if (!good_)
      return 0;
    if (left() < 1) {
      good_ = false;
      return 0;
    }
    return static_cast<std::uint8_t>(*at_++);
  }

  std::uint64_t u64() {
    if (!good_)
      return 0;
    if (left() < 8) {
      good_ = false;
      return 0;
    }
    const std::uint64_t value = load_u64(at_);
    at_ += 8;
    return value;
  }

  bool flag() {
    if (!good_)
      return false;
    const std::uint8_t value = u8();
    if (value > 1)
      good_ = false;
    return value != 0;
  }

  std::string str() {
    const std::uint64_t size = u64();
    if (!good_ || size > left()) {
      good_ = false;
      return {};
    }
    std::string out(at_, static_cast<std::size_t>(size));
    at_ += static_cast<std::size_t>(size);
    return out;
  }

  bool plausible(std::uint64_t value,
                 std::size_t minimum_bytes) const noexcept {
    return good_ && value <= left() / minimum_bytes;
  }

private:
  const char *at_;
  const char *end_;
  bool good_{true};
};

std::string reject(const std::filesystem::path &path, const std::string &why) {
  return "Not a readable comparison file: " + path.string() + " (" + why + ")";
}

} // namespace

std::string serialize_compare_result(const CompareResult &result) {
  std::string payload;
  payload.reserve(96 + result.unmatched_lines.size() * 64);

  put_u8(payload, result.success ? 1 : 0);
  put_str(payload, result.message);
  put_u8(payload, result.exact_match ? 1 : 0);
  put_u64(payload, result.unmatched_line_count);
  put_u64(payload, result.output_line_count);
  put_u64(payload, result.expect_line_count);
  put_u64(payload, result.output_hash);
  put_u64(payload, result.expect_hash);
  put_str(payload, result.warning);

  put_u64(payload, result.unmatched_lines.size());
  for (const LineDiff &line : result.unmatched_lines) {
    put_u64(payload, line.line_no);
    put_u8(payload, static_cast<std::uint8_t>(line.kind));
    put_u8(payload, line.whitespace_only ? 1 : 0);
    put_str(payload, line.output_line);
    put_str(payload, line.expect_line);

    put_u64(payload, line.token_diffs.size());
    for (const TokenDiff &token : line.token_diffs) {
      put_u64(payload, token.index);
      put_str(payload, token.output_token);
      put_str(payload, token.expect_token);
    }
  }

  std::string file;
  file.reserve(kHeaderSize + payload.size());
  file.append(kMagic, sizeof(kMagic));
  put_u8(file, kVersion);
  put_u64(file, payload.size());
  put_u64(file, XXH3_64bits(payload.data(), payload.size()));
  file.append(payload);
  return file;
}

SaveResult save_compare_result(const CompareResult &result,
                               const std::filesystem::path &path) {
  SaveResult out;

  const file::WriteResult written =
      file::write_all(path, serialize_compare_result(result));
  if (!written) {
    out.message = written.message;
    return out;
  }

  out.success = true;
  return out;
}

LoadResult load_compare_result(const std::filesystem::path &path) {
  LoadResult out;

  const file::ReadResult read = file::read_all(path);
  if (!read) {
    out.message = read.message;
    return out;
  }

  const std::string_view bytes(read.data.data(), read.data.size());
  if (bytes.size() < kHeaderSize) {
    out.message = reject(path, "too short");
    return out;
  }
  if (std::memcmp(bytes.data(), kMagic, sizeof(kMagic)) != 0) {
    out.message = reject(path, "bad magic");
    return out;
  }
  const std::uint8_t version = static_cast<std::uint8_t>(bytes[4]);
  if (version != kVersion) {
    out.message =
        reject(path, "unsupported version " +
                         std::to_string(static_cast<unsigned>(version)));
    return out;
  }

  const std::uint64_t payload_size = load_u64(bytes.data() + 5);
  const std::uint64_t payload_hash = load_u64(bytes.data() + 13);
  if (payload_size != bytes.size() - kHeaderSize) {
    out.message = reject(path, "wrong length");
    return out;
  }
  const char *payload = bytes.data() + kHeaderSize;
  if (XXH3_64bits(payload, static_cast<std::size_t>(payload_size)) !=
      payload_hash) {
    out.message = reject(path, "checksum mismatch");
    return out;
  }

  Reader reader(payload, static_cast<std::size_t>(payload_size));
  CompareResult result;

  result.success = reader.flag();
  result.message = reader.str();
  result.exact_match = reader.flag();
  result.unmatched_line_count = static_cast<std::size_t>(reader.u64());
  result.output_line_count = static_cast<std::size_t>(reader.u64());
  result.expect_line_count = static_cast<std::size_t>(reader.u64());
  result.output_hash = reader.u64();
  result.expect_hash = reader.u64();
  result.warning = reader.str();
  const std::uint64_t lines = reader.u64();
  if (!reader.plausible(lines, kMinLine)) {
    out.message =
        reject(path, reader.ok() ? "impossible line count" : "truncated");
    return out;
  }

  result.unmatched_lines.reserve(static_cast<std::size_t>(lines));
  for (std::uint64_t i = 0; i < lines; ++i) {
    LineDiff line;
    line.line_no = static_cast<std::size_t>(reader.u64());
    const std::uint8_t kind = reader.u8();
    if (reader.ok() && kind > static_cast<std::uint8_t>(LineKind::OnlyExpect)) {
      out.message = reject(path, "unknown line kind");
      return out;
    }
    line.kind = static_cast<LineKind>(kind);
    line.whitespace_only = reader.flag();
    line.output_line = reader.str();
    line.expect_line = reader.str();
    const std::uint64_t tokens = reader.u64();
    if (!reader.plausible(tokens, kMinToken)) {
      out.message =
          reject(path, reader.ok() ? "impossible token count" : "truncated");
      return out;
    }

    line.token_diffs.reserve(static_cast<std::size_t>(tokens));
    for (std::uint64_t t = 0; t < tokens; ++t) {
      TokenDiff token;
      token.index = static_cast<std::size_t>(reader.u64());
      token.output_token = reader.str();
      token.expect_token = reader.str();
      if (!reader.ok()) {
        out.message = reject(path, "truncated");
        return out;
      }
      line.token_diffs.push_back(std::move(token));
    }

    result.unmatched_lines.push_back(std::move(line));
  }

  if (reader.left() != 0) {
    out.message = reject(path, "trailing bytes");
    return out;
  }

  out.result = std::move(result);
  out.success = true;
  return out;
}
