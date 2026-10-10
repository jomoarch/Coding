#include "app/clipboard.hpp"

#include <windows.h>

#include <string>

namespace coding {
namespace clipboard {

namespace {

std::string to_utf8(const wchar_t *wide, int units) {
  if (wide == nullptr || units <= 0)
    return {};
  const int bytes = WideCharToMultiByte(CP_UTF8, 0, wide, units, nullptr, 0,
                                        nullptr, nullptr);
  if (bytes <= 0)
    return {};
  std::string out(static_cast<std::size_t>(bytes), '\0');
  WideCharToMultiByte(CP_UTF8, 0, wide, units, out.data(), bytes, nullptr,
                      nullptr);
  return out;
}

} // namespace

std::string text() {

  for (int attempt = 0; attempt < 5; ++attempt) {
    if (!OpenClipboard(nullptr)) {
      Sleep(10);
      continue;
    }

    std::string out;
    if (HANDLE data = GetClipboardData(CF_UNICODETEXT)) {
      if (const wchar_t *wide =
              static_cast<const wchar_t *>(GlobalLock(data))) {
        int units = 0;
        while (wide[units] != L'\0')
          ++units;
        out = to_utf8(wide, units);
        GlobalUnlock(data);
      }
    }
    CloseClipboard();
    return out;
  }
  return {};
}

} // namespace clipboard
} // namespace coding
