#ifndef CLEANER_HPP
#define CLEANER_HPP

#include "base\result.hpp"

#include <filesystem>
#include <string>

struct CleanerResult : ResultBase {};

CleanerResult clean_dir(const std::filesystem::path &dir,
                        const std::string &suf = "");

#endif // CLEANER_HPP