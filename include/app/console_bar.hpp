#ifndef CONSOLE_BAR_HPP
#define CONSOLE_BAR_HPP

#include "base/terminal.hpp"

#include <cstddef>
#include <string>
#include <string_view>

namespace coding {

namespace bar {

class Bar {
public:
  Bar() = default;
  ~Bar();
  Bar(const Bar &) = delete;
  Bar &operator=(const Bar &) = delete;

  bool open(term::Session &session, std::string &error);
  void close() noexcept;

  bool active() const noexcept { return active_; }

  void write_output(std::string_view data);

  void set_input(std::string_view text, std::size_t cursor_bytes);
  void set_status(std::string_view text);

  void commit_text(std::string_view text);

  void resize();

  void paint();

private:
  struct Pos {
    int row{0};
    int column{0};
  };

  void place(const Pos &at) const;
  void write_bytes(std::string_view bytes) const;
  bool cursor_now(Pos &out, int &window_top) const;
  void erase_ours();
  void hide_cursor();
  void show_cursor();
  void ensure_room(std::size_t echo_rows);
  bool skip_for_selection() const;

  void *input_{nullptr};
  void *output_{nullptr};
  bool active_{false};
  std::size_t rows_{0};
  std::size_t columns_{0};

  Pos anchor_{};
  std::string input_text_;
  std::size_t input_cursor_bytes_{0};
  std::string status_text_;

  std::string painted_input_;
  std::size_t painted_cursor_{0};
  std::string painted_status_;
  bool painted_{false};
  int painted_status_row_{-1};
  std::size_t painted_area_rows_{0};
  bool cursor_hidden_{false};
};

} // namespace bar
} // namespace coding

#endif // CONSOLE_BAR_HPP
