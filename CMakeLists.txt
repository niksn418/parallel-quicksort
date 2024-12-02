cmake_minimum_required(VERSION 3.5.0)
project(parallel_quicksort VERSION 0.1.0 LANGUAGES C CXX)

find_package(Parlay REQUIRED)

set (CMAKE_CXX_STANDARD 20)

add_executable(parallel_quicksort main.cpp)
target_link_libraries(parallel_quicksort PRIVATE Parlay::parlay)