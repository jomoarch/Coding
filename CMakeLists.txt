cmake_minimum_required(VERSION 3.11)
project(Coding LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT WIN32)
  message(FATAL_ERROR "This project only supports Windows.")
endif()

set(COMMON_SOURCES
  src/modes.cpp
  src/runner.cpp
  src/runner_single.cpp
  src/runner_batch.cpp
  src/config.cpp
  src/compiler.cpp
  src/formatter.cpp
  src/win_error.cpp
  src/iofile.cpp
  src/builder.cpp
)

set(APP_VARIANTS
  "main.cpp|cg"
  "main_batch.cpp|cg_b"
  "main_single.cpp|cg_s"
  "main_interactive.cpp|cg_i"
)

foreach(variant ${APP_VARIANTS})
  string(REPLACE "|" ";" parts "${variant}")
  list(GET parts 0 main_src)
  list(GET parts 1 exe_name)

  add_executable(${exe_name}
    src/${main_src}
    ${COMMON_SOURCES}
  )

  target_include_directories(${exe_name} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
  )

  set_target_properties(${exe_name} PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
  )
endforeach()