#ifndef INTERACTIVE_UI_HPP
#define INTERACTIVE_UI_HPP

#include "io/config.hpp"
#include "process/runner_single.hpp"

namespace coding {

namespace interactive {

bool run(const AppConfig &cfg, const SingleRunOption &base,
         SingleRunResult &out);

} // namespace interactive
} // namespace coding

#endif // INTERACTIVE_UI_HPP
