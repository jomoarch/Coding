#include "app/manager.hpp"
#include "app/prompt.hpp"
#include "app/viewer.hpp"
#include "base/color.hpp"
#include "compare/compare.hpp"
#include "store/store.hpp"
#include "io/config.hpp"
#include "io/iofile.hpp"

#include <filesystem>
#include <iostream>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

using namespace coding;

namespace {

const char *kBinaryName = "cmp_b";

bool file_exists(const std::filesystem::path &path) {
  std::error_code ec;
  return std::filesystem::exists(path, ec) &&
         std::filesystem::is_regular_file(path, ec);
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
                        "compare every case in [io].input_dir against "
                        "[io].answer_dir, then browse them");
    std::cout << "\nIt only compares; it never compiles or runs anything, so "
                 "[io].output_dir\nis left exactly as it is. After browsing it "
                 "offers to save every result\ninto [io].result_dir.\n"
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

  if (cfg.input_dir.empty()) {
    std::cerr << color::err("[compare] [io].input_dir is required: it holds "
                            "the test cases to compare (<name>.in)")
              << "\n";
    return 2;
  }
  if (cfg.output_dir.empty()) {
    std::cerr << color::err("[compare] [io].output_dir is required: it holds "
                            "the outputs to compare (<name>.out)")
              << "\n";
    return 2;
  }
  if (cfg.answer_dir.empty()) {
    std::cerr << color::err("[compare] [io].answer_dir is required: it holds "
                            "the expected answers, one <name>.ans or "
                            "<name>.out per case")
              << "\n";
    return 2;
  }

  const ComparePairResult pairs =
      gen_compare_pairs(cfg.input_dir, cfg.output_dir, cfg.answer_dir);
  if (!pairs) {
    std::cerr << color::err("[compare] ", pairs.message) << "\n";
    return 2;
  }
  if (pairs.pairs.empty()) {
    std::cerr << color::err("[compare] no *.in test cases in ", cfg.input_dir)
              << "\n";
    return 2;
  }

  std::vector<Case> cases;
  cases.reserve(pairs.pairs.size());

  std::size_t matched = 0;
  std::size_t differ = 0;
  std::size_t missing = 0;

  for (const ComparePair &pair : pairs.pairs) {
    Case item;
    item.name = pair.name;

    if (!file_exists(pair.output_path)) {
      item.state = CaseState::NoOutput;
    } else if (pair.expect_path.empty()) {
      item.state = CaseState::NoAnswer;
    } else {
      CompareOption opts;
      opts.output_path = pair.output_path;
      opts.expect_path = pair.expect_path;
      item.result = compare_output(opts);

      if (!item.result.success) {
        item.state = CaseState::Failed;
        item.note = item.result.message;
      } else if (item.result.exact_match) {
        item.state = CaseState::Identical;
      } else {
        item.state = CaseState::Differ;
      }
    }

    switch (item.state) {
    case CaseState::Identical:
      ++matched;
      break;
    case CaseState::Differ:
      ++differ;
      break;
    default:
      ++missing;
      break;
    }

    cases.push_back(std::move(item));
  }

  std::cout << color::info("[compare] ", cases.size(), " test cases: ", matched,
                           " matched, ", differ, " differ");
  if (missing != 0)
    std::cout << color::info(", ", missing, " without a result");
  std::cout << "\n";

  for (const Case &item : cases) {
    if (item.state == CaseState::NoOutput || item.state == CaseState::NoAnswer)
      std::cout << "[compare] " << item.name << ": "
                << (item.state == CaseState::NoOutput
                        ? "no output file, run cg_b first"
                        : "no answer file in " + cfg.answer_dir.string() +
                              " (tried " + item.name + ".ans and " + item.name +
                              ".out)")
                << "\n";
    else if (item.state == CaseState::Failed)
      std::cout << color::warn("[compare] ", item.name, ": ", item.note)
                << "\n";
  }

  int code = (differ != 0 || missing != 0) ? 1 : 0;
  if (viewer::view_batch(cases) != 0)
    code = 2;

  if (cfg.result_root.empty()) {
    std::cout
        << "[save] [io].result_root is not configured, nothing archived\n";
  } else if (prompt::ask_yes_no("[save] Archive this run in " +
                                (cfg.result_root / "batch").string() + " ?")) {
    store::BatchRequest request;
    request.items.reserve(cases.size());
    for (std::size_t i = 0; i < cases.size(); ++i) {
      store::BatchItem entry;
      entry.item = std::move(cases[i]);
      if (viewer::case_browsable(entry.item)) {
        entry.output_path = pairs.pairs[i].output_path;
        entry.answer_path = pairs.pairs[i].expect_path;
      }
      request.items.push_back(std::move(entry));
    }

    const store::BatchSaveOutcome saved =
        store::save_batch(cfg.result_root, request);
    if (!saved) {
      std::cerr << color::err("[save] ", saved.message) << "\n";
      code = 2;
    } else {
      std::string note;
      manager::enforce_limits(cfg, true, &note);
      std::cout << color::ok("[save] ",
                             saved.reused ? "already archived, time refreshed: "
                                          : "archived: ",
                             saved.archived, " result(s) -> ", saved.id)
                << "\n";
      if (!note.empty())
        std::cout << color::info("[save] ", note) << "\n";
    }
  }

  prompt::pause_if_needed(cli.pause);
  return code;
}
