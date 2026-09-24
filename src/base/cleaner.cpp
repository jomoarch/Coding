#include "base/cleaner.hpp"

#include <cstddef>

CleanerResult clean_dir(const std::filesystem::path &dir,
                        const std::string &suf) {
  CleanerResult r;
  r.success = false;

  std::error_code ec;

  if (!std::filesystem::exists(dir)) {
    r.message = "Directory not exist: " + dir.string();
    return r;
  }

  if (!std::filesystem::is_directory(dir, ec)) {
    r.message = "Path exists but is not a directory: " + dir.string();
    return r;
  }

  bool anything_remove = false;
  std::string ext;
  if (!suf.empty()) {
    ext = suf;
    if (ext.front() != '.')
      ext.insert(ext.begin(), '.');
  } else
    anything_remove = true;

  for (const auto &e : std::filesystem::directory_iterator(dir, ec)) {
    if (ec)
      break;
    if (anything_remove || e.path().extension() == ext) {
      std::error_code rec;
      std::filesystem::remove_all(e.path(), rec);
      if (rec) {
        r.message = "Failed to remove " + e.path().string() + ": " +
                    rec.message() + " (code " + std::to_string(rec.value()) +
                    ")";
        return r;
      }
    }
  }
  r.success = true;
  r.message = "Success";

  return r;
}