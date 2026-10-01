#include "store/detail/files.hpp"

#include <cstdint>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

namespace store::detail {

namespace fs = std::filesystem;

bool read_lines(const fs::path &path, std::vector<std::string> &out,
                std::string &error) {
  out.clear();

  std::ifstream file(path, std::ios::binary);
  if (!file) {
    std::error_code ec;
    if (!fs::exists(path, ec))
      return true;
    error = "Cannot read " + path.string();
    return false;
  }

  std::string line;
  while (std::getline(file, line)) {
    if (!line.empty() && line.back() == '\r')
      line.pop_back();
    out.push_back(std::move(line));
  }
  return true;
}

bool write_file_atomic(const fs::path &path, std::string_view text,
                       std::string &error) {
  const fs::path temp = path.string() + ".tmp";
  {
    std::ofstream file(temp, std::ios::binary | std::ios::trunc);
    if (!file) {
      error = "Cannot write " + temp.string();
      return false;
    }
    file.write(text.data(), static_cast<std::streamsize>(text.size()));
    if (!file.good()) {
      error = "Cannot write " + temp.string();
      return false;
    }
  }

  std::error_code ec;
  fs::remove(path, ec);
  ec.clear();
  fs::rename(temp, path, ec);
  if (ec) {
    error = "Cannot replace " + path.string() + ": " + ec.message();
    return false;
  }
  return true;
}

std::string local_time(std::time_t when) {
  std::tm parts{};
  const std::time_t copy = when;
#if defined(_WIN32)
  localtime_s(&parts, &copy);
#else
  parts = *std::localtime(&copy);
#endif

  char buffer[64] = {};
  if (std::strftime(buffer, sizeof(buffer), "%Y-%m-%dT%H:%M:%S%z", &parts) == 0)
    return {};
  return buffer;
}

bool file_size_of(const fs::path &path, std::uint64_t &size,
                  std::string &error) {
  std::error_code ec;
  const std::uintmax_t bytes = fs::file_size(path, ec);
  if (ec) {
    error = "Cannot read " + path.string();
    return false;
  }
  size = static_cast<std::uint64_t>(bytes);
  return true;
}

} // namespace store::detail
