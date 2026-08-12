cmake_minimum_required(VERSION 3.24)
project(reverson LANGUAGES C CXX)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)

option(REVERSON_BUILD_VST "Build the VST3 shell" OFF)

if(REVERSON_BUILD_VST)
    add_subdirectory(vst)
endif()

enable_testing()
add_executable(test_util tests/test_util.c)
target_include_directories(test_util PRIVATE core)
add_test(NAME test_util COMMAND test_util)
