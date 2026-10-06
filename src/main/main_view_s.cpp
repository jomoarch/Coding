#include "app/prompt.hpp"
#include "app/viewer.hpp"
#include "base/color.hpp"
#include "io/config.hpp"
#include "store/store.hpp"

#include <cstdio>
#include <iostream>
#include <string>
#include <vector>

using namespace coding;

namespace {

const char *kBinaryName = "view_s";

const char *kOptions =
    "      --list           print the archived comparisons, newest first\n"
    "      --id <id>        open that archived comparison instead of the "
    "newest\n"
    "      --prune          delete old comparisons instead of browsing\n"
    "      --keep <n>       how many of the newest to keep when pruning\n";

void print_history(const std::vector<store::Entry> &entries) {
  std::printf("%-16s  %-24s  %-8s  %9s  %s\n", "id", "time", "status",
              "unmatched", "name");
  for (const store::Entry &entry : entries)
    std::printf("%-16s  %-24s  %-8s  %9zu  %s\n", entry.id.c_str(),
                entry.local_time.c_str(), entry.status.c_str(), entry.unmatched,
                entry.name.c_str());
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
                        "browse the comparisons archived in [io].result_root",
                        kOptions);
    std::cout << "\nThis only reads archives; it never compares anything.\n"
                 "Exit codes: 0 identical, 1 differing, 2 config/IO error.\n";
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

    const store::ListResult listed = store::list_single(cfg.result_root);
    if (!listed) {
      std::cerr << color::err("[view] ", listed.message) << "\n";
      return 2;
    }
    if (listed.entries.size() <= cli.keep) {
      std::cout << "[prune] nothing to do: " << listed.entries.size()
                << " archived comparison(s), keeping " << cli.keep << "\n";
      prompt::pause_if_needed(cli.pause);
      return 0;
    }

    std::cout << "[prune] about to delete "
              << (listed.entries.size() - cli.keep) << " of "
              << listed.entries.size()
              << " archived comparison(s), keeping the "
              << "newest " << cli.keep << ":\n";
    for (std::size_t i = cli.keep; i < listed.entries.size(); ++i)
      std::cout << "          " << listed.entries[i].id << "  "
                << listed.entries[i].local_time << "  "
                << listed.entries[i].name << "\n";

    if (!prompt::ask_yes_no("[prune] Delete them?")) {
      std::cout << color::info("[prune] nothing was deleted") << "\n";
      prompt::pause_if_needed(cli.pause);
      return 0;
    }

    const store::PruneResult pruned =
        store::prune_single(cfg.result_root, cli.keep);
    if (!pruned) {
      std::cerr << color::err("[prune] ", pruned.message) << "\n";
      return 2;
    }
    std::cout << color::ok("[prune] removed ", pruned.removed_entries,
                           " comparison(s) and ", pruned.removed_folders,
                           " folder(s), ", pruned.freed_bytes, " bytes freed")
              << "\n";
    prompt::pause_if_needed(cli.pause);
    return 0;
  }

  if (cli.list) {
    const store::ListResult listed = store::list_single(cfg.result_root);
    if (!listed) {
      std::cerr << color::err("[view] ", listed.message) << "\n";
      return 2;
    }
    if (listed.entries.empty()) {
      std::cout << "[view] no archived comparison in "
                << (cfg.result_root / "single").string() << "\n";
      prompt::pause_if_needed(cli.pause);
      return 0;
    }
    print_history(listed.entries);
    prompt::pause_if_needed(cli.pause);
    return 0;
  }

  const store::LoadSingleResult loaded =
      store::load_single(cfg.result_root, cli.id);
  if (!loaded) {
    std::cerr << color::err("[view] ", loaded.message) << "\n";
    prompt::pause_if_needed(cli.pause);
    return 2;
  }

  if (!loaded.result.success) {
    std::cerr << color::err("[view] that archived comparison failed: ",
                            loaded.result.message)
              << "\n";
    prompt::pause_if_needed(cli.pause);
    return 2;
  }
  if (!loaded.result.warning.empty())
    std::cerr << color::warn("[view] ", loaded.result.warning) << "\n";

  if (loaded.result.exact_match) {
    std::cout << color::ok("[compare] ", loaded.entry.name, ": identical (",
                           loaded.result.output_line_count, " lines)")
              << "\n";
    prompt::pause_if_needed(cli.pause);
    return 0;
  }

  std::cout << color::info("[compare] ", loaded.entry.name, ": ",
                           loaded.result.unmatched_line_count,
                           " lines unmatched (output ",
                           loaded.result.output_line_count, ", expect ",
                           loaded.result.expect_line_count, "), rendering...")
            << "\n";
  const int rendered = viewer::view(loaded.result);

  prompt::pause_if_needed(cli.pause);
  return rendered == 0 ? 1 : 2;
}
