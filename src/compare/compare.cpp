#include "compare/compare.hpp"

#include "base/file.hpp"

#include "xxhash.h"

#include <algorithm>
#include <cstring>
#include <string_view>
#include <utility>
#include <vector>

#if defined(__SSE2__) ||                                                       \
    (defined(_MSC_VER) &&                                                      \
     (defined(_M_X64) || (defined(_M_IX86_FP) && _M_IX86_FP >= 2)))
#define COMPARE_HAVE_SSE2 1
#include <emmintrin.h>
#if defined(_MSC_VER)
#include <intrin.h>
#endif
#endif

namespace {

bool is_line_trailing_ws(char c) noexcept {
  return c == ' ' || c == '\t' || c == '\r';
}

bool is_token_sep(char c) noexcept {
  return c == ' ' || c == '\t' || c == '\r' || c == '\v' || c == '\f';
}

#if defined(COMPARE_HAVE_SSE2)
unsigned lowest_set_bit(unsigned mask) noexcept {
#if defined(_MSC_VER)
  unsigned long index = 0;
  _BitScanForward(&index, mask);
  return static_cast<unsigned>(index);
#else
  return static_cast<unsigned>(__builtin_ctz(mask));
#endif
}
#endif

const char *find_newline(const char *begin, const char *end) noexcept {
#if defined(COMPARE_HAVE_SSE2)
  const __m128i needle = _mm_set1_epi8('\n');
  while (static_cast<std::size_t>(end - begin) >= 16) {
    const __m128i chunk =
        _mm_loadu_si128(reinterpret_cast<const __m128i *>(begin));
    const unsigned mask =
        static_cast<unsigned>(_mm_movemask_epi8(_mm_cmpeq_epi8(chunk, needle)));
    if (mask != 0)
      return begin + lowest_set_bit(mask);
    begin += 16;
  }
#endif
  while (begin < end && *begin != '\n')
    ++begin;
  return begin;
}

struct Compacted {
  std::size_t size{0};
  std::size_t line_count{0};
};

Compacted compact(char *buf, std::size_t original) noexcept {
  Compacted r;
  if (original == 0)
    return r;

  std::size_t read = 0;
  if (original >= 3 && static_cast<unsigned char>(buf[0]) == 0xEF &&
      static_cast<unsigned char>(buf[1]) == 0xBB &&
      static_cast<unsigned char>(buf[2]) == 0xBF)
    read = 3; // UTF-8 BOM

  std::size_t write = 0;
  std::size_t newlines = 0;

  while (read < original) {
    const char *line_end = find_newline(buf + read, buf + original);
    const std::size_t end_offset = static_cast<std::size_t>(line_end - buf);

    std::size_t keep_end = end_offset;
    while (keep_end > read && is_line_trailing_ws(buf[keep_end - 1]))
      --keep_end;

    const std::size_t keep = keep_end - read;
    if (write != read)
      std::memmove(buf + write, buf + read, keep);
    write += keep;

    if (end_offset >= original)
      break;
    buf[write++] = '\n';
    ++newlines;
    read = end_offset + 1;
  }

  std::size_t stripped = 0;
  while (write > 0 && buf[write - 1] == '\n') {
    --write;
    ++stripped;
  }

  r.size = write;
  r.line_count = write == 0 ? 0 : 1 + (newlines - stripped);
  return r;
}

struct Token {
  std::size_t offset{0};
  std::size_t length{0};
};

void tokenize(const char *line, std::size_t size, std::vector<Token> &tokens) {
  tokens.clear();
  std::size_t i = 0;
  while (i < size) {
    while (i < size && is_token_sep(line[i]))
      ++i;
    if (i >= size)
      break;

    const std::size_t start = i;
    while (i < size && !is_token_sep(line[i]))
      ++i;

    Token token;
    token.offset = start;
    token.length = i - start;
    tokens.push_back(token);
  }
}

bool same_token(const char *a, const Token &x, const char *b,
                const Token &y) noexcept {
  return x.length == y.length &&
         std::memcmp(a + x.offset, b + y.offset, x.length) == 0;
}

void fill_token_diff(const char *output_line, std::size_t output_size,
                     const char *expect_line, std::size_t expect_size,
                     std::vector<Token> &output_tokens,
                     std::vector<Token> &expect_tokens, LineDiff &diff) {
  tokenize(output_line, output_size, output_tokens);
  tokenize(expect_line, expect_size, expect_tokens);

  const std::size_t total =
      std::max(output_tokens.size(), expect_tokens.size());

  bool any_difference = false;
  for (std::size_t i = 0; i < total; ++i) {
    const bool has_output = i < output_tokens.size();
    const bool has_expect = i < expect_tokens.size();

    if (has_output && has_expect &&
        same_token(output_line, output_tokens[i], expect_line,
                   expect_tokens[i]))
      continue;

    any_difference = true;
    TokenDiff token_diff;
    token_diff.index = i;
    if (has_output)
      token_diff.output_token.assign(output_line + output_tokens[i].offset,
                                     output_tokens[i].length);
    if (has_expect)
      token_diff.expect_token.assign(expect_line + expect_tokens[i].offset,
                                     expect_tokens[i].length);
    diff.token_diffs.push_back(std::move(token_diff));
  }

  diff.whitespace_only = !any_difference;
}

} // namespace

CompareResult compare_output(const CompareOption &opts) {
  CompareResult r;

  file::ReadResult output_read = file::read_all(opts.output_path);
  if (!output_read) {
    r.message = output_read.message;
    return r;
  }
  file::ReadResult expect_read = file::read_all(opts.expect_path);
  if (!expect_read) {
    r.message = expect_read.message;
    return r;
  }

  file::Buffer &output_raw = output_read.data;
  file::Buffer &expect_raw = expect_read.data;

  const std::size_t output_original = output_raw.size();
  const Compacted output_compacted =
      compact(output_raw.data(), output_original);
  const Compacted expect_compacted =
      compact(expect_raw.data(), expect_raw.size());
  output_raw.truncate(output_compacted.size);
  expect_raw.truncate(expect_compacted.size);

  const std::uint64_t output_hash =
      XXH3_64bits(output_raw.data(), output_raw.size());
  const std::uint64_t expect_hash =
      XXH3_64bits(expect_raw.data(), expect_raw.size());
  r.output_hash = output_hash;
  r.expect_hash = expect_hash;

  if (output_compacted.size != output_original) {
    const file::WriteResult written =
        file::write_all(opts.output_path, output_raw.view());
    if (!written)
      r.warning = written.message;
  }

  r.output_line_count = output_compacted.line_count;
  r.expect_line_count = expect_compacted.line_count;

  const bool same_content =
      output_raw.size() == expect_raw.size() && output_hash == expect_hash &&
      std::memcmp(output_raw.data(), expect_raw.data(), output_raw.size()) == 0;

  if (same_content) {
    r.success = true;
    r.exact_match = true;
    return r;
  }

  r.exact_match = false;

  const char *output = output_raw.data();
  const char *output_end = output + output_raw.size();
  const char *expect = expect_raw.data();
  const char *expect_end = expect + expect_raw.size();

  std::vector<Token> output_tokens;
  std::vector<Token> expect_tokens;

  std::size_t line_no = 0;
  while (output < output_end || expect < expect_end) {
    ++line_no;

    const bool has_output = output < output_end;
    const bool has_expect = expect < expect_end;

    const char *output_line_end =
        has_output ? find_newline(output, output_end) : output;
    const char *expect_line_end =
        has_expect ? find_newline(expect, expect_end) : expect;

    const std::size_t output_len =
        static_cast<std::size_t>(output_line_end - output);
    const std::size_t expect_len =
        static_cast<std::size_t>(expect_line_end - expect);

    LineDiff diff;
    bool unmatched = false;
    if (!has_output) {
      diff.kind = LineKind::OnlyExpect;
      unmatched = true;
    } else if (!has_expect) {
      diff.kind = LineKind::OnlyOutput;
      unmatched = true;
    } else if (output_len != expect_len ||
               std::memcmp(output, expect, output_len) != 0) {
      diff.kind = LineKind::Differ;
      unmatched = true;
    }

    if (unmatched) {
      ++r.unmatched_line_count;

      diff.line_no = line_no;
      if (has_output)
        diff.output_line.assign(output, output_len);
      if (has_expect)
        diff.expect_line.assign(expect, expect_len);

      if (diff.kind == LineKind::Differ)
        fill_token_diff(output, output_len, expect, expect_len, output_tokens,
                        expect_tokens, diff);

      r.unmatched_lines.push_back(std::move(diff));
    }

    if (has_output)
      output = output_line_end < output_end ? output_line_end + 1 : output_end;
    if (has_expect)
      expect = expect_line_end < expect_end ? expect_line_end + 1 : expect_end;
  }

  r.success = true;
  return r;
}