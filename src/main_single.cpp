#include "header.hpp"

int main() {
  std::filesystem::path config_path = "config.toml";
  auto cfg_res = load_config(config_path);
  if (!cfg_res.success) {
    std::cerr << "[config] " << cfg_res.message << "\n";
    return 2;
  }

  int code = run_single_file(cfg_res.config);

  std::cout << "\nPress any key to exit ...";
  _getch();

  return code;
}