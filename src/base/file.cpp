#include "base/file.hpp"

#include "base/handle.hpp"
#include "base/text.hpp"
#include "base/win_error.hpp"

#include <algorithm>
#include <cstdint>
#include <limits>
#include <new>

namespace file {

namespace {

constexpr std::size_t kMaxTransfer = 1u << 30;

std::string sys_error(const char *what, const std::filesystem::path &path,
                      DWORD code) {
  return text::concat(what, path, ": ", win::error_string(code));
}

} // namespace

Buffer::Buffer(std::size_t size) : size_(size) {
  if (size_ != 0)
    data_.reset(new char[size_]);
}

void Buffer::truncate(std::size_t size) noexcept {
  if (size < size_)
    size_ = size;
}

ReadResult read_all(const std::filesystem::path &path) {
  ReadResult result;

  HandleGuard h(
      CreateFileW(path.wstring().c_str(), GENERIC_READ,
                  FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                  nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr));
  if (!h.valid()) {
    result.message = sys_error("Cannot open ", path, GetLastError());
    return result;
  }

  LARGE_INTEGER file_size{};
  if (!GetFileSizeEx(h.get(), &file_size)) {
    result.message = sys_error("Cannot size ", path, GetLastError());
    return result;
  }
  if (file_size.QuadPart < 0 ||
      static_cast<std::uint64_t>(file_size.QuadPart) >
          static_cast<std::uint64_t>(std::numeric_limits<std::size_t>::max())) {
    result.message = text::concat("File is too large to read: ", path);
    return result;
  }
  const std::size_t total = static_cast<std::size_t>(file_size.QuadPart);

  try {
    result.data = Buffer(total);
  } catch (const std::bad_alloc &) {
    result.message = text::concat("Not enough memory to read ", path);
    return result;
  }

  std::size_t done = 0;
  while (done < total) {
    const DWORD want =
        static_cast<DWORD>(std::min<std::size_t>(total - done, kMaxTransfer));
    DWORD got = 0;
    if (!ReadFile(h.get(), result.data.data() + done, want, &got, nullptr)) {
      result.message = sys_error("Cannot read ", path, GetLastError());
      return result;
    }
    if (got == 0)
      break;
    done += got;
  }
  result.data.truncate(done);

  result.success = true;
  return result;
}

WriteResult write_all(const std::filesystem::path &path,
                      std::string_view data) {
  WriteResult result;

  HandleGuard h(CreateFileW(path.wstring().c_str(), GENERIC_WRITE,
                            FILE_SHARE_READ, nullptr, CREATE_ALWAYS,
                            FILE_ATTRIBUTE_NORMAL, nullptr));
  if (!h.valid()) {
    result.message = sys_error("Cannot write ", path, GetLastError());
    return result;
  }

  std::size_t done = 0;
  while (done < data.size()) {
    const DWORD want = static_cast<DWORD>(
        std::min<std::size_t>(data.size() - done, kMaxTransfer));
    DWORD wrote = 0;
    if (!WriteFile(h.get(), data.data() + done, want, &wrote, nullptr) ||
        wrote == 0) {
      result.message = sys_error("Cannot write ", path, GetLastError());
      return result;
    }
    done += wrote;
  }

  result.success = true;
  return result;
}

} // namespace file
