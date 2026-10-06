#ifndef STORE_DETAIL_LAYOUTS_HPP
#define STORE_DETAIL_LAYOUTS_HPP

#include "store/detail/index.hpp"
namespace coding {

namespace store::detail {

inline constexpr const char *kSingleColumns =
    "name\ttime\tlocal\tid\tstatus\tunmatched\toutput_lines\texpect_lines";
inline constexpr Layout kSingleLayout{kSingleColumns, 8, 3, 1};

inline constexpr const char *kBatchColumns =
    "time\tlocal\tid\tcases\tmatched\tdiffer\tunusable";
inline constexpr Layout kBatchLayout{kBatchColumns, 7, 2, 0};

inline constexpr const char *kSingleTrashColumns =
    "name\ttime\tlocal\tid\tstatus\tunmatched\toutput_lines\texpect_lines\t"
    "trashed";
inline constexpr Layout kSingleTrashLayout{kSingleTrashColumns, 9, 3, 1};
inline constexpr std::size_t kSingleTrashedColumn = 8;

inline constexpr const char *kBatchTrashColumns =
    "time\tlocal\tid\tcases\tmatched\tdiffer\tunusable\ttrashed";
inline constexpr Layout kBatchTrashLayout{kBatchTrashColumns, 8, 2, 0};
inline constexpr std::size_t kBatchTrashedColumn = 7;

inline constexpr const char *kSingleDir = "single";
inline constexpr const char *kBatchDir = "batch";
inline constexpr const char *kTrashDir = ".trash";

inline constexpr const char *kResultFile = "result.cmp";
inline constexpr const char *kOutputFile = "output.txt";
inline constexpr const char *kAnswerFile = "answer.txt";
inline constexpr const char *kMetaFile = "meta.txt";
inline constexpr const char *kManifestFile = "manifest.tsv";
inline constexpr const char *kPinFile = "pinned.tsv";

} // namespace store::detail

} // namespace coding

#endif // STORE_DETAIL_LAYOUTS_HPP
