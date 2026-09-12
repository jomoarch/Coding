#include "iofile.hpp"

#include <algorithm>
#include <cstddef>
#include <fstream>

namespace {

struct NameKey {
  std::string class_key;
  std::vector<std::string> digits;
};

bool is_digit(char c) { return c >= '0' && c <= '9'; }

NameKey make_name_key(const std::string &stem) {
  NameKey k;
  for (std::size_t i = 0; i < stem.size();) {
    if (is_digit(stem[i])) {
      std::size_t j = i;
      while (j < stem.size() && is_digit(stem[j]))
        ++j;
      std::size_t p = i;
      while (p + 1 < j && stem[p] == '0')
        ++p;
      k.digits.emplace_back(stem.substr(p, j - p));
      k.class_key += '\x01';
      i = j;
    } else {
      std::size_t j = i;
      while (j < stem.size() && !is_digit(stem[j]))
        ++j;
      k.class_key.append(stem, i, j - i);
      i = j;
    }
  }
  return k;
}

bool compare_name_key(const NameKey &a, const NameKey &b) {
  if (a.class_key != b.class_key) {
    return a.class_key < b.class_key;
  }
  for (std::size_t i = 0; i < a.digits.size(); ++i) {
    const auto &x = a.digits[i];
    const auto &y = b.digits[i];
    if (x.size() != y.size())
      return x.size() < y.size();
    if (x != y)
      return x < y;
  }
  return false;
}

bool identical(const NameKey &a, const NameKey &b) {
  return a.class_key == b.class_key && a.digits == b.digits;
}

} // namespace

IOFileResult gen_filepair(const IOFileOption &opts) {
  IOFileResult r;
  r.success = false;

  std::error_code ec;
  if (!std::filesystem::exists(opts.input_dir, ec) ||
      !std::filesystem::is_directory(opts.input_dir, ec)) {
    r.message = "Input dir not found: " + opts.input_dir.string();
    return r;
  }

  if (!std::filesystem::exists(opts.output_dir, ec)) {
    std::filesystem::create_directory(opts.output_dir, ec);
    if (ec) {
      r.message = "Failed to create output dir: " + ec.message();
      return r;
    }
  } else {
    if (!std::filesystem::is_directory(opts.output_dir, ec)) {
      r.message = "Output path exists but is not a directory: " +
                  opts.output_dir.string();
      return r;
    }
    for (const auto &e :
         std::filesystem::directory_iterator(opts.output_dir, ec)) {
      if (ec)
        break;
      std::error_code rec;
      std::filesystem::remove_all(e.path(), rec);
      if (rec) {
        r.message = "Failed to remove " + e.path().string() + ": " +
                    rec.message() + " (code " + std::to_string(rec.value()) +
                    ")";
        return r;
      }
    }
  }

  struct Item {
    std::string name;
    std::filesystem::path in_path;
    NameKey key;
  };

  std::vector<Item> items;
  for (const auto &e :
       std::filesystem::directory_iterator(opts.input_dir, ec)) {
    if (ec)
      break;
    if (!e.is_regular_file(ec))
      continue;
    if (e.path().extension() != ".in")
      continue;
    std::string stem = e.path().stem().string();
    items.push_back({stem, e.path(), make_name_key(stem)});
  }

  std::sort(items.begin(), items.end(), [](const Item &a, const Item &b) {
    return compare_name_key(a.key, b.key) < 0;
  });

  for (std::size_t i = 1; i < items.size(); ++i) {
    if (identical(items[i - 1].key, items[i].key)) {
      r.message =
          "Duplicate name (same structure and numbers): " + items[i - 1].name +
          " vs " + items[i].name;
      return r;
    }
  }

  r.pairs.reserve(items.size());
  for (const auto &item : items) {
    FilePair p;
    p.name = item.name;
    p.input_path = item.in_path;
    p.output_path = opts.output_dir / (item.name + ".out");
    std::ofstream ofs(p.output_path, std::ios::binary | std::ios::trunc);
    if (!ofs) {
      r.message = "Failed to create output file: " + p.output_path.string();
      return r;
    }
    r.pairs.push_back(std::move(p));
  }

  r.success = true;
  r.message = "Success";
  return r;
}