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
                     graph/rt_timestamp.cpp
                     crdt/crdt_operations.cpp
                     synchronization/graph_synchronization.cpp
                     synchronization/type_translation.cpp
                     synchronization/graph_signals.cpp
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
                            Qt6::Core
                            Eigen3::Eigen
                            fastdds
                            fastcdr)

