#ifndef STALE_EXE_HPP
#define STALE_EXE_HPP

#include <filesystem>
#include <string>

namespace stale_exe {

enum class Outcome { Cleared, Parked, Blocked };

Outcome clear(const std::filesystem::path &exe, bool park, std::string &note);

} // namespace stale_exe

#endif // STALE_EXE_HPP
