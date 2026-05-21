cmake_minimum_required(VERSION 3.10)
project(pl0 C)

set(CMAKE_C_STANDARD 11)

file(GLOB_RECURSE SRC "src/*.c")

add_executable(pl0 ${SRC})

target_include_directories(pl0 PRIVATE
        src
        src/Parser
        src/Scanner
)

set_target_properties(pl0  PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}"
)