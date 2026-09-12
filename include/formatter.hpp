#ifndef FORMATTER_HPP
#define FORMATTER_HPP

#include "runner.hpp"
#include "runner_batch.hpp"

#include <vector>

std::vector<std::string> format_results(const std::vector<ResultUnit> &results,
                                        bool show_message = true,
                                        bool show_wall_time = false);

#endif // FORMATTER_HPP