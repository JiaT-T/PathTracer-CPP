# Keep this directory usable as a CMake entry point for existing local workflows.
cmake_minimum_required(VERSION 3.21)
project(PathTracerCPPEntry LANGUAGES CXX)
add_subdirectory("${CMAKE_CURRENT_LIST_DIR}/.." "${CMAKE_CURRENT_BINARY_DIR}/renderer")
