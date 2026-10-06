#ifndef STORE_HPP
#define STORE_HPP

#include "base/result.hpp"
#include "compare/case.hpp"

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>
namespace coding {

namespace store {

constexpr std::size_t kIdLength = 16;

struct Entry {
  std::string name;
  std::string id;
  std::int64_t time{0};
  std::string local_time;
  std::string status;
  std::size_t unmatched{0};
  std::size_t output_lines{0};
  std::size_t expect_lines{0};
};

struct SaveRequest {
  std::string name;
  const CompareResult *result{nullptr};
  std::filesystem::path output_path;
  std::filesystem::path answer_path;
};

struct SaveOutcome : ResultBase {
  std::string id;
  bool reused{false};
  std::string local_time;
};

[[nodiscard]] SaveOutcome save_single(const std::filesystem::path &root,
                                      const SaveRequest &request);

struct ListResult : ResultBase {
  std::vector<Entry> entries;
};

[[nodiscard]] ListResult list_single(const std::filesystem::path &root);

struct LoadSingleResult : ResultBase {
  Entry entry;
  CompareResult result;
  std::filesystem::path output_path;
  std::filesystem::path answer_path;
};

[[nodiscard]] LoadSingleResult load_single(const std::filesystem::path &root,
                                           const std::string &id);

struct BatchItem {
  Case item;
  std::filesystem::path output_path;
  std::filesystem::path answer_path;
};

struct BatchRequest {
  std::vector<BatchItem> items;
};

struct BatchSaveOutcome : ResultBase {
  std::string id;
  bool reused{false};
  std::size_t archived{0};
  std::string local_time;
};

[[nodiscard]] BatchSaveOutcome save_batch(const std::filesystem::path &root,
                                          const BatchRequest &request);

struct BatchEntry {
  std::string id;
  std::int64_t time{0};
  std::string local_time;
  std::size_t cases{0};
  std::size_t matched{0};
  std::size_t differ{0};
  std::size_t unusable{0};
};

struct BatchListResult : ResultBase {
  std::vector<BatchEntry> entries;
};

[[nodiscard]] BatchListResult list_batch(const std::filesystem::path &root);

struct BatchLoadResult : ResultBase {
  BatchEntry entry;
  std::vector<Case> cases;
};

[[nodiscard]] BatchLoadResult load_batch(const std::filesystem::path &root,
                                         const std::string &id);

struct PruneResult : ResultBase {
  std::size_t removed_entries{0};
  std::size_t removed_folders{0};
  std::uintmax_t freed_bytes{0};
};

[[nodiscard]] PruneResult prune_single(const std::filesystem::path &root,
                                       std::size_t keep);
[[nodiscard]] PruneResult prune_batch(const std::filesystem::path &root,
                                      std::size_t keep);

struct PinEntry {
  std::string id;
  std::string name;
};

struct PinListResult : ResultBase {
  std::vector<PinEntry> entries;
};

[[nodiscard]] PinListResult list_pins_single(const std::filesystem::path &root);
[[nodiscard]] PinListResult list_pins_batch(const std::filesystem::path &root);

[[nodiscard]] ResultBase pin_single(const std::filesystem::path &root,
                                    const std::string &id);
[[nodiscard]] ResultBase unpin_single(const std::filesystem::path &root,
                                      const std::string &id);
[[nodiscard]] ResultBase pin_batch(const std::filesystem::path &root,
                                   const std::string &id);
[[nodiscard]] ResultBase unpin_batch(const std::filesystem::path &root,
                                     const std::string &id);

struct TrashEntry {
  std::string id;
  std::string name;
  std::string status;
  std::int64_t time{0};
  std::string local_time;
  std::size_t unmatched{0};
  std::size_t count{0};
  std::int64_t trashed_time{0};
  std::string trashed_local;
};

struct TrashListResult : ResultBase {
  std::vector<TrashEntry> entries;
  std::uintmax_t bytes{0};
};

[[nodiscard]] TrashListResult
list_trash_single(const std::filesystem::path &root);
[[nodiscard]] TrashListResult
list_trash_batch(const std::filesystem::path &root);

struct TrashMoveResult : ResultBase {
  std::string name;
};

[[nodiscard]] TrashMoveResult trash_single(const std::filesystem::path &root,
                                           const std::string &id);
[[nodiscard]] TrashMoveResult trash_batch(const std::filesystem::path &root,
                                          const std::string &id);

[[nodiscard]] ResultBase restore_single(const std::filesystem::path &root,
                                        const std::string &id);
[[nodiscard]] ResultBase restore_batch(const std::filesystem::path &root,
                                       const std::string &id);

struct EraseResult : ResultBase {
  std::size_t removed{0};
  std::uintmax_t freed_bytes{0};
};

[[nodiscard]] EraseResult erase_trash_single(const std::filesystem::path &root,
                                             const std::string &id);
[[nodiscard]] EraseResult erase_trash_batch(const std::filesystem::path &root,
                                            const std::string &id);
[[nodiscard]] EraseResult empty_trash_single(const std::filesystem::path &root);
[[nodiscard]] EraseResult empty_trash_batch(const std::filesystem::path &root);

struct TrashLimitResult : ResultBase {
  std::size_t removed{0};
  std::uintmax_t freed_bytes{0};
  std::uintmax_t bytes{0};
};

[[nodiscard]] TrashLimitResult
limit_trash_single(const std::filesystem::path &root, std::uintmax_t max_bytes);
[[nodiscard]] TrashLimitResult
limit_trash_batch(const std::filesystem::path &root, std::uintmax_t max_bytes);

struct CountLimitResult : ResultBase {
  std::size_t moved{0};
};

[[nodiscard]] CountLimitResult
enforce_single_limit(const std::filesystem::path &root,
                     std::size_t max_unprotected);
[[nodiscard]] CountLimitResult
enforce_batch_limit(const std::filesystem::path &root,
                    std::size_t max_unprotected);

} // namespace store

} // namespace coding

#endif // STORE_HPP
