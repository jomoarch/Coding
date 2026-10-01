#include "app/prompt.hpp"
#include "app/viewer.hpp"
#include "base/color.hpp"
#include "compare/serialize.hpp"
#include "io/config.hpp"
#include "io/iofile.hpp"

#include <algorithm>
#include <filesystem>
#include <iostream>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

namespace {

const char *kBinaryName = "view_b";

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
        kBinaryName, "browse the comparison results saved in [io].result_dir");
    std::cout << "\nThis only reads results; it never compares anything.\n"
                 "Exit codes: 0 all identical, 1 something differs, 2 "
                 "config/IO error.\n";
    return 0;
  }

  ConfigResult cfg_res = load_config(cli.config_path);
  if (!cfg_res) {
    std::cerr << color::err("[config] ", cfg_res.message) << "\n";
    return 2;
  }
  const AppConfig &cfg = cfg_res.config;

  if (cfg.result_dir.empty()) {
    std::cerr << color::err(
                     "[view] [io].result_dir is required: it is the directory "
                     "of saved comparison results to browse")
              << "\n";
    return 2;
  }

  std::error_code ec;
  if (!std::filesystem::is_directory(cfg.result_dir, ec)) {
    std::cerr << color::err("[view] result dir not found: ", cfg.result_dir)
              << "\n";
    return 2;
  }

  std::vector<std::filesystem::path> files;
  for (const auto &entry :
       std::filesystem::directory_iterator(cfg.result_dir, ec)) {
    if (ec)
      break;
    if (!entry.is_regular_file(ec))
      continue;
    if (entry.path().extension() != ".cmp")
      continue;
    files.push_back(entry.path());
  }
  std::sort(files.begin(), files.end(),
            [](const std::filesystem::path &a, const std::filesystem::path &b) {
              return case_name_less(a.stem().string(), b.stem().string());
            });

  if (files.empty()) {
    std::cerr << color::err("[view] no *.cmp results in ", cfg.result_dir)
              << "\n";
    return 2;
  }

  std::vector<viewer::Case> cases;
  cases.reserve(files.size());

  std::size_t matched = 0;
  std::size_t differ = 0;
  std::size_t unusable = 0;

  for (const std::filesystem::path &file : files) {
    viewer::Case item;
    item.name = file.stem().string();

    LoadResult loaded = load_compare_result(file);
    if (!loaded) {
      item.state = viewer::CaseState::Unreadable;
      item.note = loaded.message;
    } else if (!loaded.result.success) {
      item.state = viewer::CaseState::Failed;
      item.note = loaded.result.message;
    } else {
      item.result = std::move(loaded.result);
      item.state = item.result.exact_match ? viewer::CaseState::Identical
                                           : viewer::CaseState::Differ;
    }

    switch (item.state) {
    case viewer::CaseState::Identical:
      ++matched;
      break;
    case viewer::CaseState::Differ:
      ++differ;
      break;
    default:
      ++unusable;
      break;
    }

    cases.push_back(std::move(item));
  }

  std::cout << color::info("[view] ", cases.size(), " saved results: ", matched,
                           " matched, ", differ, " differ");
  if (unusable != 0)
    std::cout << color::info(", ", unusable, " unusable");
  std::cout << "\n";

  for (const viewer::Case &item : cases) {
    if (item.state == viewer::CaseState::Unreadable)
      std::cerr << color::warn("[view] ", item.name,
                               ": cannot read the saved result: ", item.note)
                << "\n";
    else if (item.state == viewer::CaseState::Failed)
      std::cerr << color::warn(
                       "[view] ", item.name,
                       ": that result is a failed comparison: ", item.note)
                << "\n";
  }

  int code = (differ != 0 || unusable != 0) ? 1 : 0;
  if (viewer::view_batch(cases) != 0)
    code = 2;

  prompt::pause_if_needed(cli.pause);
  return code;
}
