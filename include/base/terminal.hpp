#ifndef TERMINAL_HPP
#define TERMINAL_HPP

#include <cstddef>
#include <string>
#include <string_view>

namespace term {

enum class Key {
  Up,
  Down,
  Left,
  Right,
  ViewUp,
  ViewDown,
  Enter,
  ShiftEnter,
  Collapse,
  CollapseAll,
  Quit,
  Resize,
};

struct Size {
  std::size_t columns{0};
  std::size_t rows{0};
};

class Session {
public:
  Session() = default;
  ~Session();
  Session(Session &&other) noexcept;
  Session &operator=(Session &&other) noexcept;
  Session(const Session &) = delete;
  Session &operator=(const Session &) = delete;

  static bool open(Session &out, std::string &error);

  void close() noexcept;

  Size size() const;
  void write(std::string_view frame) const;
  bool read(Key &out) const;

private:
  void *input_{nullptr};
  void *output_{nullptr};
  unsigned long saved_input_mode_{0};
  unsigned int saved_output_cp_{0};
  bool active_{false};
};

} // namespace term

#endif // TERMINAL_HPP
