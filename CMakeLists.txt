cmake_minimum_required(VERSION 3.11)
project(Coding LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT WIN32)
  message(FATAL_ERROR "This project only supports Windows.")
endif()

set(COMMON_SOURCES
  src/text.cpp
  src/app.cpp
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
  src/color.cpp
  src/cleaner.cpp
)

add_library(coding_common STATIC ${COMMON_SOURCES})
target_include_directories(coding_common PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}/include
)

if(MSVC)
  target_compile_options(coding_common PRIVATE /W4 /permissive-)
else()
  target_compile_options(coding_common PRIVATE -Wall -Wextra)
endif()

function(add_coding_app exe_name main_src)
  add_executable(${exe_name} ${main_src})
  target_link_libraries(${exe_name} PRIVATE coding_common)
  set_target_properties(${exe_name} PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
  )
endfunction()

add_coding_app(cg_b src/main_batch.cpp)
add_coding_app(cg_s src/main_single.cpp)
add_coding_app(cg_i src/main_interactive.cpp)
add_coding_app(clean_dir src/main_clean_dir.cpp)
