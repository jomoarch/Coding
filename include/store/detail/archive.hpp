#ifndef STORE_DETAIL_ARCHIVE_HPP
#define STORE_DETAIL_ARCHIVE_HPP

#include "base/hash.hpp"
#include "compare/compare.hpp"

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>
namespace coding {

namespace store::detail {

bool payload_id(const CompareResult &result,
                const std::filesystem::path &output_path,
                const std::filesystem::path &answer_path, std::string &id,
                std::string &result_bytes, std::string &error);

bool finish_id(hash::Sha256 &hasher, std::string &id, std::string &error);

bool copy_file(const std::filesystem::path &from,
               const std::filesystem::path &to, std::string &error);

class TempFolder {
public:
  bool create(const std::filesystem::path &parent, std::string &error);
  const std::filesystem::path &path() const noexcept { return path_; }
  void keep() noexcept { keep_ = true; }
  ~TempFolder();

  TempFolder() = default;
  TempFolder(const TempFolder &) = delete;
  TempFolder &operator=(const TempFolder &) = delete;

private:
  std::filesystem::path path_;
  bool keep_{false};
};

bool publish(const std::filesystem::path &temp,
             const std::filesystem::path &target, std::string &error);

bool looks_like_id(std::string_view name) noexcept;

std::uintmax_t folder_bytes(const std::filesystem::path &folder);

} // namespace store::detail

} // namespace coding

#endif // STORE_DETAIL_ARCHIVE_HPP
