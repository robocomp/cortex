cmake_minimum_required(VERSION 3.10)
project(dsr
        VERSION 2021.3.1
        DESCRIPTION "Robocomp DSR library"
        LANGUAGES CXX)

include(GNUInstallDirs)

add_definitions(-I/usr/include/x86_64-linux-gnu/qt6/QtOpenGLWidgets/)

include_directories(/home/robocomp/robocomp/classes)
add_subdirectory(api)
add_subdirectory(core)
add_subdirectory(gui)
add_subdirectory(python-wrapper)


if (SANITIZER)

    set (CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -fno-omit-frame-pointer -fsanitize=address,undefined")
    set (CMAKE_LINKER_FLAGS_DEBUG "${CMAKE_LINKER_FLAGS_DEBUG} -fno-omit-frame-pointer -fsanitize=address,undefined")

endif()

if (WITH_TESTS)

add_subdirectory(tests)

endif()

if (WITH_BENCHMARKS)

add_subdirectory(bench)

endif()