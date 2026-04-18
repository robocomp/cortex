cmake_minimum_required(VERSION 3.10)
project(dsr
        VERSION 2021.3.1
        DESCRIPTION "Robocomp DSR library"
        LANGUAGES CXX)

include(GNUInstallDirs)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# -DTSAN=ON  enables ThreadSanitizer across the whole build (library + tests/benchmarks).
# Must be applied before any add_subdirectory so every TU is instrumented.
# Incompatible with SANITIZER (ASan/UBSan) — enforce that here.
option(TSAN "Enable ThreadSanitizer" OFF)
if (TSAN)
    if (SANITIZER)
        message(FATAL_ERROR "TSAN and SANITIZER (ASan+UBSan) are mutually exclusive")
    endif()
    message(STATUS "ThreadSanitizer enabled")
    add_compile_options(-fsanitize=thread -fno-omit-frame-pointer)
    add_link_options(-fsanitize=thread)
endif()

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

add_subdirectory(benchmarks)

endif()
