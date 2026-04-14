cmake_minimum_required(VERSION 3.10)
project(PL0 C)

set(CMAKE_C_STANDARD 11)

file(GLOB SRCS "*.c" "**/*.c")

add_executable(pl0 ${SRCS})

set_target_properties(pl0 PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/target"
)

target_link_libraries(pl0 m) # math.h