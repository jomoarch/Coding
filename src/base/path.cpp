#include "base/path.hpp"

#include <system_error>
#include <string>
namespace coding {

std::filesystem::path compress_path(const std::filesystem::path &p,
                                    std::error_code &ec, bool resolve_symlinks,
                                    bool make_absolute) {
  ec.clear();
  if (p.empty())
    return {};

  std::filesystem::path result = p;

  if (make_absolute && !result.is_absolute()) {
    std::error_code abs_ec;
    std::filesystem::path abs = std::filesystem::absolute(result, abs_ec);
    if (abs_ec) {
      ec = abs_ec;
    } else {
      result = std::move(abs);
    }
  }

  if (resolve_symlinks) {
    std::error_code weak_ec;
    std::filesystem::path weak =
        std::filesystem::weakly_canonical(result, weak_ec);
    if (weak_ec) {
      if (!ec)
        ec = weak_ec;
      result = result.lexically_normal();
    } else {
      result = std::move(weak);
    }
  } else {
    result = result.lexically_normal();
  }

  if (result.has_filename() && result.filename().empty())
    result = result.parent_path();

  return result;
}

std::filesystem::path compress_path(const std::filesystem::path &p,
                                    bool resolve_symlinks, bool make_absolute) {
  std::error_code ec;
  return compress_path(p, ec, resolve_symlinks, make_absolute);
}

} // namespace coding