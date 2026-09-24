#ifndef HANDLE_HPP
#define HANDLE_HPP

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>

struct HandleGuard {
  HANDLE h = INVALID_HANDLE_VALUE;
  HandleGuard() = default;
  explicit HandleGuard(HANDLE handle) noexcept : h(handle) {}
  ~HandleGuard() { reset(); }
  HandleGuard(const HandleGuard &) = delete;
  HandleGuard &operator=(const HandleGuard &) = delete;
  HandleGuard(HandleGuard &&o) noexcept : h(o.h) { o.h = INVALID_HANDLE_VALUE; }
  HandleGuard &operator=(HandleGuard &&o) noexcept {
    if (this != &o) {
      reset(o.h);
      o.h = INVALID_HANDLE_VALUE;
    }
    return *this;
  }
  explicit operator bool() const noexcept { return valid(); }

  HANDLE get() const noexcept { return h; }
  HANDLE *put() noexcept { return &h; }
  bool valid() const noexcept { return h && h != INVALID_HANDLE_VALUE; }
  void reset(HANDLE handle = INVALID_HANDLE_VALUE) noexcept {
    if (valid())
      ::CloseHandle(h);
    h = handle;
  }
};

#endif // HANDLE_HPP