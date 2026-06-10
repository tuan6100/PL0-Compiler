cmake_minimum_required(VERSION 3.10)
project(PL0 C)

set(CMAKE_C_STANDARD 11)

file(GLOB_RECURSE SRC "src/*.c")

add_executable(pl0 ${SRC})

#target_include_directories(pl0 PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

set_target_properties(pl0  PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}"
)