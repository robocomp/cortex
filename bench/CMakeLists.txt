cmake_minimum_required( VERSION 3.10 )
project(dsr_benchmarl
        VERSION 2025.01.15
        DESCRIPTION "DSR Benchmarks"
        LANGUAGES CXX)


Include(FetchContent)

set(BENCHMARK_ENABLE_GTEST_TESTS false)

FetchContent_Declare(
  googlebenchmark
  GIT_REPOSITORY https://github.com/google/benchmark.git
  GIT_TAG        v1.9.1
)

FetchContent_MakeAvailable(googlebenchmark)

#include(~/software/vcpkg/scripts/buildsystems/vcpkg.cmake)

find_package(Qt6 COMPONENTS Core REQUIRED)
find_package(Eigen3 3.3 REQUIRED NO_MODULE)

add_executable(bench test.cpp
        utils.h)

set_target_properties(bench PROPERTIES
CMAKE_CXX_STANDARD 23
CXX_STANDARD_REQUIRED ON
CXX_EXTENSIONS ON)

target_compile_options(bench PUBLIC -g -std=c++23)


target_link_libraries(bench PRIVATE
                            benchmark::benchmark
                            Robocomp::dsr_api
                            Robocomp::dsr_core
                            Qt6::Core
                            Eigen3::Eigen
                            fastdds
                            fastcdr)

