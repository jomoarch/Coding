#ifndef HASH_HPP
#define HASH_HPP

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace hash {

class Sha256 {
public:
  Sha256();
  ~Sha256();
  Sha256(Sha256 &&other) noexcept;
  Sha256 &operator=(Sha256 &&other) noexcept;
  Sha256(const Sha256 &) = delete;
  Sha256 &operator=(const Sha256 &) = delete;

  void write(const void *data, std::size_t size);
  void write(std::string_view text);
  void write_u64(std::uint64_t value);

  std::string hex();

  bool ok() const noexcept { return state_ != nullptr; }

private:
  void *state_{nullptr};
  void *object_{nullptr};
  void *algorithm_{nullptr};
};

} // namespace hash

#endif // HASH_HPP
