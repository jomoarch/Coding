#ifndef CLEANER_HPP
#define CLEANER_HPP

#include <filesystem>
#include <string>

struct CleanerResult {
  bool success{false};
  std::string message;

  bool ok() const noexcept { return success; }
  explicit operator bool() const noexcept { return success; }
};

CleanerResult clean_dir(const std::filesystem::path &dir,
                        const std::string &suf = "");

#endif // CLEANER_HPP