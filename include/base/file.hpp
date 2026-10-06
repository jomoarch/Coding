#ifndef FILE_HPP
#define FILE_HPP

#include "base/result.hpp"

#include <cstddef>
#include <filesystem>
#include <memory>
#include <string_view>
namespace coding {

namespace file {

class Buffer {
public:
  Buffer() = default;
  explicit Buffer(std::size_t size);

  Buffer(Buffer &&) noexcept = default;
  Buffer &operator=(Buffer &&) noexcept = default;
  Buffer(const Buffer &) = delete;
  Buffer &operator=(const Buffer &) = delete;

  char *data() noexcept { return data_.get(); }
  const char *data() const noexcept { return data_.get(); }
  std::size_t size() const noexcept { return size_; }
  bool empty() const noexcept { return size_ == 0; }

  std::string_view view() const noexcept {
    return size_ == 0 ? std::string_view()
                      : std::string_view(data_.get(), size_);
  }

  void truncate(std::size_t size) noexcept;

private:
  std::unique_ptr<char[]> data_;
  std::size_t size_{0};
};

struct ReadResult : ResultBase {
  Buffer data;
};

struct WriteResult : ResultBase {};

[[nodiscard]] ReadResult read_all(const std::filesystem::path &path);

[[nodiscard]] WriteResult write_all(const std::filesystem::path &path,
                                    std::string_view data);

} // namespace file

} // namespace coding

#endif // FILE_HPP
