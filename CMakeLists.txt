cmake_minimum_required(VERSION 3.11)
project(Coding LANGUAGES C CXX)

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
  src/base/file.cpp
  src/base/terminal.cpp
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
  src/app/prompt.cpp
  src/app/viewer.cpp
)

set(COMPARE_SOURCES
  src/compare/compare.cpp
  src/compare/serialize.cpp
)

set(THIRD_PARTY_SOURCES
  src/third_party/xxhash.c
)

add_library(coding_third_party STATIC
  ${THIRD_PARTY_SOURCES}
)

target_include_directories(coding_third_party PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/include/third_party
)

add_library(coding_common STATIC
  ${BASE_SOURCES}
  ${IO_SOURCES}
  ${PROCESS_SOURCES}
  ${COMPARE_SOURCES}
  ${APP_SOURCES}
)

target_link_libraries(coding_common PUBLIC coding_third_party)

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
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/bin"
  )
endfunction()

add_coding_app(cg_b       src/main/main_batch.cpp)
add_coding_app(cg_s       src/main/main_single.cpp)
add_coding_app(cg_i       src/main/main_interactive.cpp)
add_coding_app(clean_dir  src/main/main_clean_dir.cpp)
add_coding_app(cmp_s      src/main/main_cmp_s.cpp)
add_coding_app(view_s     src/main/main_view_s.cpp)
