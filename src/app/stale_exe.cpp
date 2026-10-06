#include "app/stale_exe.hpp"

#include "base/hash.hpp"
#include "base/win_error.hpp"

#include <cstdio>
#include <ctime>
#include <fstream>
#include <system_error>
namespace coding {

namespace fs = std::filesystem;

namespace {

bool file_sha256(const fs::path &path, std::string &out) {
  std::ifstream file(path, std::ios::binary);
  if (!file)
    return false;

  hash::Sha256 hasher;
  char buf[65536];
  while (file) {
    file.read(buf, sizeof(buf));
    const std::streamsize n = file.gcount();
    if (n > 0)
      hasher.write(buf, static_cast<std::size_t>(n));
  }
  out = hasher.hex();
  return !out.empty();
}

fs::path unique_target(const fs::path &dir, const std::string &name) {
  std::error_code ec;
  fs::path target = dir / name;
  for (int n = 2; n < 1000 && fs::exists(target, ec); ++n)
    target = dir / (name + "-" + std::to_string(n));
  return target;
}

} // namespace

namespace stale_exe {

Outcome clear(const fs::path &exe, bool park, std::string &note) {
  std::error_code ec;
  if (exe.empty() || !fs::exists(exe, ec))
    return Outcome::Cleared;

  ec.clear();
  fs::remove(exe, ec);
  if (!ec) {
    ec.clear();
    if (!fs::exists(exe, ec))
      return Outcome::Cleared;
  }

  const std::string reason =
      ec ? win::error_string(static_cast<DWORD>(ec.value()))
         : std::string("the file is still open");
  const fs::path dir = exe.parent_path() / ".trash";

  if (!park) {
    note = "cannot delete " + exe.string() + ": " + reason;
    return Outcome::Blocked;
  }

  ec.clear();
  fs::create_directories(dir, ec);
  if (ec) {
    note = "cannot delete " + exe.string() + ": " + reason +
           ", and cannot create " + dir.string() + ": " +
           win::error_string(static_cast<DWORD>(ec.value()));
    return Outcome::Blocked;
  }

  std::string name;
  if (!file_sha256(exe, name))
    name = "unreadable-" +
           std::to_string(static_cast<long long>(std::time(nullptr)));

  const fs::path target = unique_target(dir, name);
  ec.clear();
  fs::rename(exe, target, ec);
  if (ec) {
    note = "cannot delete " + exe.string() + ": " + reason +
           ", and cannot move it to " + target.string() + ": " +
           win::error_string(static_cast<DWORD>(ec.value()));
    return Outcome::Blocked;
  }

  note = "cannot delete " + exe.string() + ": " + reason + "; moved it to " +
         target.string();
  return Outcome::Parked;
}

} // namespace stale_exe
} // namespace coding
