#ifndef PROMPT_HPP
#define PROMPT_HPP

#include <cstddef>
#include <filesystem>
#include <string>
#include <string_view>
namespace coding {

namespace prompt {

struct Options {
  std::string config_path{"config.toml"};
  int pause{-1};
  bool help{false};

  bool list{false};
  std::string id;
  bool prune{false};
  std::size_t keep{0};
  bool keep_given{false};
};

bool parse(int argc, char **argv, Options &out, std::string &error);

void print_usage(const char *binary, const char *summary,
                 const char *extra_options = nullptr);

bool should_pause(int pause) noexcept;

bool interactive_stdin() noexcept;

void pause_if_needed(int pause);

bool ask_save(const std::filesystem::path &target, std::string_view what);

bool ask_yes_no(std::string_view question);

} // namespace prompt

} // namespace coding

#endif // PROMPT_HPP
