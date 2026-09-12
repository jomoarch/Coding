cmake_minimum_required(VERSION 3.11)
project(Coding LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_executable(Coding
  src/main.cpp
  src/runner.cpp
  src/runner_batch.cpp
  src/config.cpp
  src/compiler.cpp
  src/formatter.cpp
  src/win_error.cpp
  src/iofile.cpp
  src/builder.cpp
)

target_include_directories(Coding PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/include
)

set_target_properties(Coding PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
)

if(NOT WIN32)
  message(FATAL_ERROR "This project only supports Windows.")
endif()