cmake_minimum_required(VERSION 3.24)
project(reverson LANGUAGES C CXX)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)

option(REVERSON_BUILD_VST "Build the VST3 shell" OFF)

if(REVERSON_BUILD_VST)
    add_subdirectory(vst)
endif()

include(CTest)

function(reverson_add_test name)
    add_executable(${name} tests/${name}.c)
    target_include_directories(${name} PRIVATE core)
    target_link_libraries(${name} PRIVATE reverson_core)
    add_test(NAME ${name} COMMAND ${name})
endfunction()

add_library(reverson_core STATIC core/rev_delay.c)
target_include_directories(reverson_core PUBLIC core)
if(MSVC)
    target_compile_options(reverson_core PRIVATE /W4)
else()
    target_compile_options(reverson_core PRIVATE -Wall -Wextra)
endif()

if(BUILD_TESTING)
    add_executable(test_util tests/test_util.c)
    target_include_directories(test_util PRIVATE core)
    add_test(NAME test_util COMMAND test_util)
    reverson_add_test(test_delay)
endif()
