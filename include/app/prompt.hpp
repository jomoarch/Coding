#ifndef PROMPT_HPP
#define PROMPT_HPP

#include <filesystem>
#include <string>
#include <string_view>

namespace prompt {

struct Options {
  std::string config_path{"config.toml"};
  int pause{-1};
  bool help{false};
};

bool parse(int argc, char **argv, Options &out, std::string &error);

void print_usage(const char *binary, const char *summary);

bool should_pause(int pause) noexcept;

void pause_if_needed(int pause);

bool ask_save(const std::filesystem::path &target, std::string_view what);

} // namespace prompt

#endif // PROMPT_HPP
