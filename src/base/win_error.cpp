#include "base/win_error.hpp"

#include <vector>

namespace win {

std::string error_string(DWORD code) {
  if (code == 0)
    return "OK";

  LPWSTR buf = nullptr;
  DWORD n = FormatMessageW(
      FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
          FORMAT_MESSAGE_IGNORE_INSERTS,
      nullptr, code, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
      reinterpret_cast<LPWSTR>(&buf), 0, nullptr);

  std::string r;
  if (n && buf) {
    while (n > 0 && (buf[n - 1] == L'\r' || buf[n - 1] == L'\n'))
      --n;
    int sz = WideCharToMultiByte(CP_UTF8, 0, buf, static_cast<int>(n), nullptr,
                                 0, nullptr, nullptr);
    r.resize(sz);
    WideCharToMultiByte(CP_UTF8, 0, buf, static_cast<int>(n), r.data(), sz,
                        nullptr, nullptr);
  }
  LocalFree(buf);
  return r + " (Code " + std::to_string(static_cast<int>(code)) + ")";
}

std::string last_error_string() { return error_string(GetLastError()); }

} // namespace win