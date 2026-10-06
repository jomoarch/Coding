#ifndef RESULT_HPP
#define RESULT_HPP

#include <string>
namespace coding {

struct ResultBase {
  bool success{false};
  std::string message;

  bool ok() const noexcept { return success; }
  explicit operator bool() const noexcept { return success; }
};

} // namespace coding

#endif // RESULT_HPP