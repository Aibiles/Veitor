cmake_minimum_required(VERSION 3.5.0)
project(Veitor VERSION 0.1.0 LANGUAGES C)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

add_executable(Veitor  ./src/vorpal.c )


