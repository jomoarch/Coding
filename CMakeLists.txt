cmake_minimum_required(VERSION 3.11)
project(Coding LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT WIN32)
  message(FATAL_ERROR "This project only supports Windows.")
endif()

set(BASE_SOURCES
  src/base/text.cpp
  src/base/color.cpp
  src/base/win_error.cpp
  src/base/cleaner.cpp
)

set(IO_SOURCES
  src/io/config.cpp
  src/io/iofile.cpp
)

set(PROCESS_SOURCES
  src/process/compiler.cpp
  src/process/runner.cpp
  src/process/runner_single.cpp
  src/process/runner_batch.cpp
  src/process/process_launcher.cpp
)

set(APP_SOURCES
  src/app/app.cpp
  src/app/builder.cpp
  src/app/formatter.cpp
  src/app/modes.cpp
)

add_library(coding_common STATIC
  ${BASE_SOURCES}
  ${IO_SOURCES}
  ${PROCESS_SOURCES}
  ${APP_SOURCES}
)

target_include_directories(coding_common PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}/include
  ${CMAKE_CURRENT_SOURCE_DIR}/include/third_party
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

add_coding_app(cg_b       src/main/main_batch.cpp)
add_coding_app(cg_s       src/main/main_single.cpp)
add_coding_app(cg_i       src/main/main_interactive.cpp)
add_coding_app(clean_dir  src/main/main_clean_dir.cpp)
