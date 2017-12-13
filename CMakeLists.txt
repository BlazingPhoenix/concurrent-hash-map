cmake_minimum_required(VERSION 3.7)
project(concurrent_hash_map LANGUAGES CXX)

add_definitions(-std=c++2a)

add_subdirectory(concurrent_hash_map)
add_subdirectory(examples)
enable_testing()
add_subdirectory(tests)
