#ifndef MODES_HPP
#define MODES_HPP

#include "config.hpp"

int run_interactive(const AppConfig &cfg);
int run_single_file(const AppConfig &cfg);
int run_batch(const AppConfig &cfg);

#endif // MODES_HPP
