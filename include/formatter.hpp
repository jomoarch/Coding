#ifndef FORMATTER_HPP
#define FORMATTER_HPP

#include "runner.hpp"
#include "runner_batch.hpp"
#include <iostream>
#include <ostream>

#include <cstddef>
#include <string>
#include <vector>

std::vector<std::string> format_results(const std::vector<ResultUnit> &results,
                                        bool show_message = true,
                                        bool show_wall_time = false,
                                        std::ostream &os = std::cout);

std::string format_memory(std::size_t bytes);

#endif // FORMATTER_HPP