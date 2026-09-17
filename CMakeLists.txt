cmake_minimum_required(VERSION 3.20)
project(myproject LANGUAGES CXX)

set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

add_subdirectory(optlib)
add_subdirectory(tasks/setcover)
add_subdirectory(tasks/knapsack)
add_subdirectory(tasks/coloring)
add_subdirectory(tasks/tsp)
add_subdirectory(tasks/vrp)
