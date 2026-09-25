#ifndef PROBE_H
#define PROBE_H

#ifdef _WIN32

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>
#include <io.h>
#include <fcntl.h>

#include <cstdarg>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <mutex>
#include <streambuf>
#include <string>

namespace probe {

inline constexpr char kStdoutTag = '\x01';
inline constexpr char kStderrTag = '\x02';

namespace detail {

inline constexpr std::size_t kBufSize = 64 * 1024;

inline void write_all(HANDLE handle, const char *data, std::size_t n) noexcept {
  while (n > 0) {
    const DWORD chunk = static_cast<DWORD>(
        n < (1u << 30) ? n : static_cast<std::size_t>(1u << 30));
    DWORD written = 0;
    if (!::WriteFile(handle, data, chunk, &written, nullptr) || written == 0)
      return;
    data += written;
    n -= written;
  }
}

struct Sink {
  HANDLE handle;
  char buf[kBufSize];
  std::size_t used;
  char current_tag;
  std::mutex mu;

  Sink() noexcept
      : handle(::GetStdHandle(STD_OUTPUT_HANDLE)), used(0), current_tag(0) {}

  void flush_locked() noexcept {
    if (used == 0)
      return;
    write_all(handle, buf, used);
    used = 0;
    current_tag = 0;
  }

  void flush() noexcept {
    std::lock_guard<std::mutex> lock(mu);
    flush_locked();
  }

  void push(char tag, const char *data, std::size_t n) noexcept {
    if (data == nullptr || n == 0)
      return;

    std::lock_guard<std::mutex> lock(mu);

    if (n >= kBufSize) {
      flush_locked();
      write_all(handle, &tag, 1);
      write_all(handle, data, n);
      current_tag = tag;
      return;
    }

    if (used + (tag != current_tag ? 1u : 0u) + n > kBufSize)
      flush_locked();

    if (tag != current_tag) {
      buf[used++] = tag;
      current_tag = tag;
    }
    std::memcpy(buf + used, data, n);
    used += n;

    if (std::memchr(data, '\n', n) != nullptr)
      flush_locked();
  }
};

inline Sink &sink() noexcept;

inline void flush_at_exit() noexcept { sink().flush(); }

inline Sink &sink() noexcept {
  static Sink *s = nullptr;
  if (s == nullptr) {
    s = new Sink();
    (void)std::atexit(&flush_at_exit);
  }
  return *s;
}

} // namespace detail

inline void flush() noexcept { detail::sink().flush(); }

class TaggedBuf final : public std::streambuf {
public:
  explicit TaggedBuf(char tag) noexcept : tag_(tag) {}

protected:
  int overflow(int ch) override {
    if (ch != EOF) {
      char c = static_cast<char>(static_cast<unsigned char>(ch));
      detail::sink().push(tag_, &c, 1);
    }
    return traits_type::not_eof(ch);
  }

  std::streamsize xsputn(const char *s, std::streamsize n) override {
    if (s != nullptr && n > 0)
      detail::sink().push(tag_, s, static_cast<std::size_t>(n));
    return n;
  }

  int sync() override {
    detail::sink().flush();
    return 0;
  }

private:
  char tag_;
};

class Installer {
public:
  Installer() {
    ::_setmode(1, _O_BINARY);
    ::_setmode(2, _O_BINARY);
    std::ios::sync_with_stdio(false);

    static TaggedBuf cout_buf(kStdoutTag);
    static TaggedBuf cerr_buf(kStderrTag);
    static TaggedBuf clog_buf(kStderrTag);

    std::cout.rdbuf(&cout_buf);
    std::cerr.rdbuf(&cerr_buf);
    std::clog.rdbuf(&clog_buf);
  }
};

inline void ensure_installed() {
  static Installer inst;
  (void)inst;
}

} // namespace probe

namespace {
struct AutoInstall {
  AutoInstall() { probe::ensure_installed(); }
};
static AutoInstall g_auto_install;
} // namespace

extern "C" inline void probe_flush(void) { probe::flush(); }

extern "C" inline int probe_printf(const char *fmt, ...) {
  if (fmt == nullptr)
    return -1;
  char stack_buf[4096];
  va_list ap;
  va_start(ap, fmt);
  int n = ::vsnprintf(stack_buf, sizeof(stack_buf), fmt, ap);
  va_end(ap);
  if (n < 0)
    return n;
  if (static_cast<std::size_t>(n) < sizeof(stack_buf)) {
    probe::detail::sink().push(probe::kStdoutTag, stack_buf,
                               static_cast<std::size_t>(n));
    return n;
  }
  std::string heap;
  heap.resize(static_cast<std::size_t>(n) + 1);
  va_start(ap, fmt);
  ::vsnprintf(heap.data(), heap.size(), fmt, ap);
  va_end(ap);
  probe::detail::sink().push(probe::kStdoutTag, heap.data(),
                             static_cast<std::size_t>(n));
  return n;
}

extern "C" inline int probe_fprintf(FILE *stream, const char *fmt, ...) {
  if (stream == nullptr || fmt == nullptr)
    return -1;
  char tag;
  if (stream == stdout)
    tag = probe::kStdoutTag;
  else if (stream == stderr)
    tag = probe::kStderrTag;
  else {
    va_list ap;
    va_start(ap, fmt);
    int r = ::vfprintf(stream, fmt, ap);
    va_end(ap);
    return r;
  }
  char stack_buf[8192];
  va_list ap;
  va_start(ap, fmt);
  int n = ::vsnprintf(stack_buf, sizeof(stack_buf), fmt, ap);
  va_end(ap);
  if (n < 0)
    return n;
  if (static_cast<std::size_t>(n) < sizeof(stack_buf)) {
    probe::detail::sink().push(tag, stack_buf, static_cast<std::size_t>(n));
    return n;
  }
  std::string heap;
  heap.resize(static_cast<std::size_t>(n) + 1);
  va_start(ap, fmt);
  ::vsnprintf(heap.data(), heap.size(), fmt, ap);
  va_end(ap);
  probe::detail::sink().push(tag, heap.data(), static_cast<std::size_t>(n));
  return n;
}

extern "C" inline int probe_puts(const char *text) {
  if (text == nullptr)
    return EOF;
  std::size_t n = std::strlen(text);
  probe::detail::sink().push(probe::kStdoutTag, text, n);
  probe::detail::sink().push(probe::kStdoutTag, "\n", 1);
  return static_cast<int>(n + 1);
}

extern "C" inline int probe_fputs(const char *text, FILE *stream) {
  if (text == nullptr || stream == nullptr)
    return EOF;
  char tag;
  if (stream == stdout)
    tag = probe::kStdoutTag;
  else if (stream == stderr)
    tag = probe::kStderrTag;
  else
    return ::fputs(text, stream);
  std::size_t n = std::strlen(text);
  probe::detail::sink().push(tag, text, n);
  return static_cast<int>(n);
}

extern "C" inline int probe_putchar(int ch) {
  if (ch == EOF)
    return EOF;
  char c = static_cast<char>(static_cast<unsigned char>(ch));
  probe::detail::sink().push(probe::kStdoutTag, &c, 1);
  return ch;
}

extern "C" inline int probe_fputc(int ch, FILE *stream) {
  if (stream == nullptr || ch == EOF)
    return EOF;
  char tag;
  if (stream == stdout)
    tag = probe::kStdoutTag;
  else if (stream == stderr)
    tag = probe::kStderrTag;
  else
    return ::fputc(ch, stream);
  char c = static_cast<char>(static_cast<unsigned char>(ch));
  probe::detail::sink().push(tag, &c, 1);
  return ch;
}

extern "C" inline std::size_t probe_fwrite(const void *ptr, std::size_t size,
                                           std::size_t count, FILE *stream) {
  if (stream == nullptr)
    return 0;
  char tag;
  if (stream == stdout)
    tag = probe::kStdoutTag;
  else if (stream == stderr)
    tag = probe::kStderrTag;
  else
    return ::fwrite(ptr, size, count, stream);
  std::size_t n = size * count;
  if (n != 0)
    probe::detail::sink().push(tag, static_cast<const char *>(ptr), n);
  return count;
}

extern "C" inline int probe_fflush(FILE *stream) {
  if (stream == stdout || stream == stderr) {
    probe::detail::sink().flush();
    return 0;
  }
  if (stream == nullptr) {
    probe::detail::sink().flush();
    return ::fflush(nullptr);
  }
  return ::fflush(stream);
}

extern "C" inline int probe_scanf(const char *fmt, ...) {
  probe::flush();
  if (fmt == nullptr)
    return EOF;
  va_list ap;
  va_start(ap, fmt);
  int n = ::vscanf(fmt, ap);
  va_end(ap);
  return n;
}

extern "C" inline int probe_getchar(void) {
  probe::flush();
  return ::getchar();
}

#ifdef printf
#undef printf
#endif
#ifdef fprintf
#undef fprintf
#endif
#ifdef puts
#undef puts
#endif
#ifdef fputs
#undef fputs
#endif
#ifdef putchar
#undef putchar
#endif
#ifdef fputc
#undef fputc
#endif
#ifdef putc
#undef putc
#endif
#ifdef fwrite
#undef fwrite
#endif
#ifdef fflush
#undef fflush
#endif
#ifdef scanf
#undef scanf
#endif
#ifdef getchar
#undef getchar
#endif

#define printf(...) probe_printf(__VA_ARGS__)
#define fprintf(stream, ...) probe_fprintf(stream, __VA_ARGS__)
#define puts(text) probe_puts(text)
#define fputs(text, stream) probe_fputs(text, stream)
#define putchar(ch) probe_putchar(ch)
#define fputc(ch, stream) probe_fputc(ch, stream)
#define putc(ch, stream) probe_fputc(ch, stream)
#define fwrite(ptr, size, count, stream) probe_fwrite(ptr, size, count, stream)
#define fflush(stream) probe_fflush(stream)
#define scanf(...) probe_scanf(__VA_ARGS__)
#define getchar() probe_getchar()

#endif // _WIN32

#endif // PROBE_H
