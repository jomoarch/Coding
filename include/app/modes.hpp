#ifndef MODES_HPP
#define MODES_HPP

#include "io/config.hpp"
namespace coding {

int run_interactive(const AppConfig &cfg);
int run_single_file(const AppConfig &cfg);
int run_batch(const AppConfig &cfg);

} // namespace coding

#endif // MODES_HPP
