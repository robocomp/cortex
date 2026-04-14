cmake_minimum_required(VERSION 3.10)
project(dsr_benchmarks
        VERSION 2024.12.01
        DESCRIPTION "DSR Benchmarking Suite"
        LANGUAGES CXX)

# Fetch Catch2 if not already available
Include(FetchContent)

FetchContent_Declare(
  Catch2
  GIT_REPOSITORY https://github.com/catchorg/Catch2.git
  GIT_TAG        v3.8.0
)

FetchContent_Declare(
  nanobench
  GIT_REPOSITORY https://github.com/martinus/nanobench.git
  GIT_TAG        v4.3.11
)

FetchContent_MakeAvailable(Catch2 nanobench)

# Find required packages
find_package(Boost REQUIRED)
find_package(Qt6 COMPONENTS Core REQUIRED)
find_package(Eigen3 3.3 REQUIRED NO_MODULE)

# Collect source files
set(BENCHMARK_SOURCES
    benchmark_main.cpp

    # Latency benchmarks
    latency/delta_propagation_bench.cpp
    latency/signal_latency_bench.cpp
    latency/crdt_join_bench.cpp

    # Throughput benchmarks
    throughput/single_agent_ops_bench.cpp
    throughput/concurrent_writers_bench.cpp
    throughput/single_agent_ops_with_latency_bench.cpp
    throughput/query_ops_bench.cpp

    # Scalability benchmarks
    scalability/multi_agent_sync_bench.cpp
    scalability/graph_size_impact_bench.cpp
    scalability/thread_scaling_bench.cpp
    scalability/graph_size_scaling_bench.cpp
    scalability/agent_scaling_bench.cpp

    # Consistency benchmarks
    consistency/convergence_time_bench.cpp
    consistency/conflict_rate_bench.cpp
)

# Header files for IDE integration
set(BENCHMARK_HEADERS
    core/benchmark_config.h
    core/timing_utils.h
    core/metrics_collector.h
    core/nanobench_adapter.h
    core/report_generator.h
    fixtures/multi_agent_fixture.h
    fixtures/graph_generator.h
)

# Create benchmark executable
add_executable(dsr_benchmarks
    ${BENCHMARK_SOURCES}
    ${BENCHMARK_HEADERS}
)

# Set C++ standard
set_target_properties(dsr_benchmarks PROPERTIES
    CMAKE_CXX_STANDARD 23
    CXX_STANDARD_REQUIRED ON
    CXX_EXTENSIONS ON
)

target_compile_options(dsr_benchmarks PUBLIC -g -std=c++23)

# Include directories
target_include_directories(dsr_benchmarks PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/core
    ${CMAKE_CURRENT_SOURCE_DIR}/fixtures
)

# Link libraries
target_link_libraries(dsr_benchmarks PRIVATE
    Catch2::Catch2
    nanobench
    dsr_api
    dsr_core
    Qt6::Core
    Eigen3::Eigen
    fastdds
    fastcdr
)

# Create results directory
file(MAKE_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/results)

# Copy results directory structure
add_custom_command(TARGET dsr_benchmarks POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory $<TARGET_FILE_DIR:dsr_benchmarks>/results
    COMMENT "Creating results directory"
)

# Register tests with CTest (optional)
# Disabled auto-discovery as it requires running the binary at build time
# which may fail if libraries are not in LD_LIBRARY_PATH
# include(Catch)
# catch_discover_tests(dsr_benchmarks)

# Installation (optional)
install(TARGETS dsr_benchmarks
    RUNTIME DESTINATION bin
)

# Print configuration summary
message(STATUS "")
message(STATUS "DSR Benchmarks Configuration:")
message(STATUS "  Build type: ${CMAKE_BUILD_TYPE}")
message(STATUS "  C++ Standard: C++23")
message(STATUS "  Catch2 version: 3.8.0")
message(STATUS "  nanobench version: 4.3.11")
message(STATUS "")
