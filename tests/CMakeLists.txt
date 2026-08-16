cmake_minimum_required( VERSION 3.10 )
project(dsr_tests
        VERSION 2024.11.12
        DESCRIPTION "DSR Tests"
        LANGUAGES CXX)


Include(FetchContent)

FetchContent_Declare(
  Catch2
  GIT_REPOSITORY https://github.com/catchorg/Catch2.git
  GIT_TAG        v3.8.0
)

FetchContent_MakeAvailable(Catch2)

#include(~/software/vcpkg/scripts/buildsystems/vcpkg.cmake)


find_package(Boost REQUIRED)
find_package(Qt6 COMPONENTS Core REQUIRED)
find_package(Eigen3 3.3 REQUIRED NO_MODULE)



add_executable(tests test.cpp
                     graph/edge_operations.cpp
                     graph/node_operations.cpp
                     graph/graph_operations.cpp
                     graph/attribute_operations.cpp
                     graph/convenience_operations.cpp
                     graph/inner_gaussian.cpp
                     graph/rt_timestamp.cpp
                     graph/rt_covariance.cpp
                     crdt/crdt_operations.cpp
                     synchronization/graph_synchronization.cpp
                     synchronization/type_translation.cpp
                     synchronization/lww_sync_engine.cpp
                     synchronization/graph_signals.cpp
                     synchronization/fake_network_tests.cpp
                     transport/fake_network.h
                     utils.h)


set_target_properties(tests PROPERTIES
CMAKE_CXX_STANDARD 23
CXX_STANDARD_REQUIRED ON
CXX_EXTENSIONS ON)

target_compile_options(tests PUBLIC -g -std=c++23)


target_link_libraries(tests PRIVATE
                            Catch2::Catch2WithMain
                            Robocomp::dsr_api
                            Robocomp::dsr_core
                            cortex_profiling
                            Qt6::Core
                            Eigen3::Eigen
                            fastdds
                            fastcdr)

# ── Perfetto integration tests ────────────────────────────────────────────────
# Built only when the Perfetto backend is active (any mode).
if (TARGET cortex_profiling)
    get_target_property(_pf_defs cortex_profiling INTERFACE_COMPILE_DEFINITIONS)
    if (_pf_defs AND _pf_defs MATCHES "CORTEX_PROFILING_BACKEND_PERFETTO")
        add_executable(perfetto_tests
            profiling/perfetto_test.cpp)

        set_target_properties(perfetto_tests PROPERTIES
            CXX_STANDARD 23
            CXX_STANDARD_REQUIRED ON)

        target_compile_options(perfetto_tests PRIVATE -g -std=c++23)

        target_link_libraries(perfetto_tests PRIVATE
            Catch2::Catch2
            Robocomp::dsr_api
            Robocomp::dsr_core
            cortex_profiling)
    endif()
endif()
