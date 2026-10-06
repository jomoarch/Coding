#ifndef MANAGER_HPP
#define MANAGER_HPP

#include "app/prompt.hpp"
#include "io/config.hpp"
namespace coding {

namespace manager {

enum class Kind { Records, Trash, Pins };

[[nodiscard]] int run(Kind kind, bool batch, const prompt::Options &cli,
                      const AppConfig &cfg);

void enforce_limits(const AppConfig &cfg, bool batch,
                    std::string *note = nullptr);

} // namespace manager

} // namespace coding

#endif // MANAGER_HPP
