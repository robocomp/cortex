cmake_minimum_required(VERSION 3.10)

project( dsr_tests
        VERSION 1.0.0
        DESCRIPTION "Robocomp DSR test"
        LANGUAGES CXX )

find_package(Qt5 COMPONENTS Core REQUIRED)
find_package (Eigen3 3.3 REQUIRED NO_MODULE)
find_package(Catch2 3 REQUIRED)

#add_custom_target(dsr_tests)
add_executable( dsr_tests src/test_dsr_core.cpp)

set(CMAKE_BUILD_TYPE Debug)

target_sources(dsr_tests
        PRIVATE
        src/test_dsr_api.cpp
        )


target_link_libraries(dsr_tests
        PRIVATE
        pthread Qt5::Core dsr_core dsr_gui dsr_api  fastcdr fastrtps osgDB OpenThreads Eigen3::Eigen Catch2::Catch2WithMain)


target_include_directories(dsr_tests
        PRIVATE
        # TODO: Don't like, try to fix
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/../../../classes/>
        PUBLIC
        # Own headers
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include/>
        $<INSTALL_INTERFACE:>
        )

set_target_properties(dsr_tests PROPERTIES
        CMAKE_CXX_STANDARD 20
        CXX_STANDARD_REQUIRED ON
        CXX_EXTENSIONS ON)

target_compile_options(dsr_tests PUBLIC -Ofast -g -fmax-errors=5 -std=c++20 -fno-char8_t)





install(FILES ${EXECUTABLE_OUTPUT_PATH}/dsr_tests DESTINATION ${RC_COMPONENT_INSTALL_PATH}/bin/ PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE GROUP_READ GROUP_EXECUTE WORLD_READ WORLD_EXECUTE )
