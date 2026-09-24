#ifndef RESULT_HPP
#define RESULT_HPP

#include <string>

struct ResultBase {
  bool success{false};
  std::string message;

  bool ok() const noexcept { return success; }
  explicit operator bool() const noexcept { return success; }
};

#endif // RESULT_HPP