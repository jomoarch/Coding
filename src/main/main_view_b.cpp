#include "app/prompt.hpp"
#include "app/viewer.hpp"
#include "base/color.hpp"
#include "io/config.hpp"
#include "store/store.hpp"

#include <cstdio>
#include <iostream>
#include <string>
#include <vector>

namespace {

const char *kBinaryName = "view_b";

const char *kOptions =
    "      --list           print the archived runs, newest first\n"
    "      --run <id>       open that run instead of the newest\n"
    "      --prune          delete old runs instead of browsing\n"
    "      --keep <n>       how many of the newest runs to keep when pruning\n";

void print_history(const std::vector<store::BatchEntry> &entries) {
  std::printf("%-16s  %-24s  %6s  %8s  %8s  %9s\n", "id", "time", "cases",
              "matched", "differ", "unusable");
  for (const store::BatchEntry &entry : entries)
    std::printf("%-16s  %-24s  %6zu  %8zu  %8zu  %9zu\n", entry.id.c_str(),
                entry.local_time.c_str(), entry.cases, entry.matched,
                entry.differ, entry.unusable);
}

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
    prompt::print_usage(kBinaryName,
                        "browse the batch runs archived in [io].result_root",
                        kOptions);
    std::cout << "\nThis only reads archives; it never compares anything.\n"
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

  if (cfg.result_root.empty()) {
    std::cerr << color::err("[view] [io].result_root is required: it is the "
                            "archive of comparisons")
              << "\n";
    return 2;
  }

  if (cli.prune) {
    if (!cli.keep_given) {
      std::cerr << color::err(
                       "[view] --prune needs --keep <n>: nothing is ever "
                       "deleted without being told how much to keep")
                << "\n";
      return 2;
    }

    const store::BatchListResult listed = store::list_batch(cfg.result_root);
    if (!listed) {
      std::cerr << color::err("[view] ", listed.message) << "\n";
      return 2;
    }
    if (listed.entries.size() <= cli.keep) {
      std::cout << "[prune] nothing to do: " << listed.entries.size()
                << " archived run(s), keeping " << cli.keep << "\n";
      prompt::pause_if_needed(cli.pause);
      return 0;
    }

    std::cout << "[prune] about to delete "
              << (listed.entries.size() - cli.keep) << " of "
              << listed.entries.size()
              << " archived run(s), keeping the newest " << cli.keep << ":\n";
    for (std::size_t i = cli.keep; i < listed.entries.size(); ++i)
      std::cout << "          " << listed.entries[i].id << "  "
                << listed.entries[i].local_time << "  "
                << listed.entries[i].cases << " cases\n";

    if (!prompt::ask_yes_no("[prune] Delete them?")) {
      std::cout << color::info("[prune] nothing was deleted") << "\n";
      prompt::pause_if_needed(cli.pause);
      return 0;
    }

    const store::PruneResult pruned =
        store::prune_batch(cfg.result_root, cli.keep);
    if (!pruned) {
      std::cerr << color::err("[prune] ", pruned.message) << "\n";
      return 2;
    }
    std::cout << color::ok("[prune] removed ", pruned.removed_entries,
                           " run(s) and ", pruned.removed_folders,
                           " folder(s), ", pruned.freed_bytes, " bytes freed")
              << "\n";
    prompt::pause_if_needed(cli.pause);
    return 0;
  }

  if (cli.list) {
    const store::BatchListResult listed = store::list_batch(cfg.result_root);
    if (!listed) {
      std::cerr << color::err("[view] ", listed.message) << "\n";
      return 2;
    }
    if (listed.entries.empty()) {
      std::cout << "[view] no archived run in "
                << (cfg.result_root / "batch").string() << "\n";
      prompt::pause_if_needed(cli.pause);
      return 0;
    }
    print_history(listed.entries);
    prompt::pause_if_needed(cli.pause);
    return 0;
  }

  const store::BatchLoadResult loaded =
      store::load_batch(cfg.result_root, cli.id);
  if (!loaded) {
    std::cerr << color::err("[view] ", loaded.message) << "\n";
    prompt::pause_if_needed(cli.pause);
    return 2;
  }

  std::size_t differ = 0;
  std::size_t unusable = 0;
  for (const Case &item : loaded.cases) {
    if (item.state == CaseState::Differ)
      ++differ;
    else if (item.state != CaseState::Identical)
      ++unusable;
  }

  std::cout << color::info("[view] ", loaded.cases.size(),
                           " saved results: ", loaded.entry.matched,
                           " matched, ", differ, " differ");
  if (unusable != 0)
    std::cout << color::info(", ", unusable, " unusable");
  std::cout << "\n";

  for (const Case &item : loaded.cases) {
    if (item.state == CaseState::Unreadable)
      std::cerr << color::warn("[view] ", item.name,
                               ": cannot read the saved result: ", item.note)
                << "\n";
    else if (item.state == CaseState::Failed)
      std::cerr << color::warn(
                       "[view] ", item.name,
                       ": that result is a failed comparison: ", item.note)
                << "\n";
  }

  int code = (differ != 0 || unusable != 0) ? 1 : 0;
  if (viewer::view_batch(loaded.cases) != 0)
    code = 2;

  prompt::pause_if_needed(cli.pause);
  return code;
}
