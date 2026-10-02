#include "app/manager.hpp"
#include "app/prompt.hpp"
#include "base/color.hpp"
#include "io/config.hpp"

#include <iostream>
#include <string>

namespace {

constexpr const char *kBinaryName = "pin_s";

constexpr const char *kOptions =
    "      --id <id>        ignored here; the list is browsed in the tool\n";

} // namespace

int main(int argc, char **argv) {
  color::install();

  prompt::Options cli;
  std::string error;
  if (!prompt::parse(argc, argv, cli, error)) {
    std::cerr << color::err(kBinaryName, ": ", error) << "\n"
              << "Try '" << kBinaryName << " --help' for usage.\n";
    return 2;
  }
  if (cli.help) {
    prompt::print_usage(
        kBinaryName, "manage the protect list of the single archive", kOptions);
    std::cout << "\nj/k or arrows move, ctrl+j/k scroll, Enter opens, q or Esc "
                 "quits, : for commands.\n";
    return 0;
  }

  ConfigResult cfg_res = load_config(cli.config_path);
  if (!cfg_res) {
    std::cerr << color::err("[config] ", cfg_res.message) << "\n";
    return 2;
  }

  return manager::run(manager::Kind::Pins, false, cli, cfg_res.config);
}