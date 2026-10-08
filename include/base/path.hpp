#ifndef PATH_HPP
#define PATH_HPP

#include <filesystem>

namespace coding {

[[nodiscard]] std::filesystem::path
compress_path(const std::filesystem::path &p, std::error_code &ec,
              bool resolve_symlinks = false, bool make_absolute = false);

[[nodiscard]] std::filesystem::path
compress_path(const std::filesystem::path &p, bool resolve_symlinks = false,
              bool make_absolute = false);

} // namespace coding

#endif // PATH_HPP