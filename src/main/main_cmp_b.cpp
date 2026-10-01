#include "app/prompt.hpp"
#include "app/viewer.hpp"
#include "base/color.hpp"
#include "compare/compare.hpp"
#include "compare/serialize.hpp"
#include "io/config.hpp"
#include "io/iofile.hpp"

#include <filesystem>
#include <iostream>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

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

  if (cfg.answer_dir.empty()) {
    std::cerr << color::err("[compare] [io].answer_dir is required: it holds "
                            "the expected answers, one <name>.out per case")
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

  std::vector<viewer::Case> cases;
  cases.reserve(pairs.pairs.size());

  std::size_t matched = 0;
  std::size_t differ = 0;
  std::size_t missing = 0;

  for (const ComparePair &pair : pairs.pairs) {
    viewer::Case item;
    item.name = pair.name;

    if (!file_exists(pair.output_path)) {
      item.state = viewer::CaseState::NoOutput;
    } else if (!file_exists(pair.expect_path)) {
      item.state = viewer::CaseState::NoAnswer;
    } else {
      CompareOption opts;
      opts.output_path = pair.output_path;
      opts.expect_path = pair.expect_path;
      item.result = compare_output(opts);

      if (!item.result.success) {
        item.state = viewer::CaseState::Failed;
        item.note = item.result.message;
      } else if (item.result.exact_match) {
        item.state = viewer::CaseState::Identical;
      } else {
        item.state = viewer::CaseState::Differ;
      }
    }

    switch (item.state) {
    case viewer::CaseState::Identical:
      ++matched;
      break;
    case viewer::CaseState::Differ:
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

  for (const viewer::Case &item : cases) {
    if (item.state == viewer::CaseState::NoOutput ||
        item.state == viewer::CaseState::NoAnswer)
      std::cout << "[compare] " << item.name << ": "
                << (item.state == viewer::CaseState::NoOutput
                        ? "no output file, run cg_b first"
                        : "no expected answer")
                << "\n";
    else if (item.state == viewer::CaseState::Failed)
      std::cout << color::warn("[compare] ", item.name, ": ", item.note)
                << "\n";
  }

  int code = (differ != 0 || missing != 0) ? 1 : 0;
  if (viewer::view_batch(cases) != 0)
    code = 2;

  if (cfg.result_dir.empty()) {
    std::cout << "[save] [io].result_dir is not configured, nothing saved\n";
  } else if (prompt::ask_save(cfg.result_dir, "results")) {
    std::error_code ec;
    std::filesystem::create_directories(cfg.result_dir, ec);

    std::size_t saved = 0;
    std::size_t failed = 0;
    for (const viewer::Case &item : cases) {
      if (!viewer::case_browsable(item))
        continue;

      const SaveResult written = save_compare_result(
          item.result, cfg.result_dir / (item.name + ".cmp"));
      if (!written) {
        std::cerr << color::err("[save] ", item.name, ": ", written.message)
                  << "\n";
        ++failed;
      } else {
        ++saved;
      }
    }

    std::cout << color::ok("[save] saved ", saved, " result(s) -> ",
                           cfg.result_dir);
    if (failed != 0)
      std::cout << color::err(" (", failed, " failed)");
    std::cout << "\n";

    if (failed != 0)
      code = 2;
  }

  prompt::pause_if_needed(cli.pause);
  return code;
}
