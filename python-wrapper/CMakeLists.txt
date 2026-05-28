cmake_minimum_required( VERSION 3.10 )
project(python_wrapper
        VERSION 1.0.0
        DESCRIPTION "Robocomp dsr python wrapper library"
        LANGUAGES CXX )

find_package(Qt6 COMPONENTS Core REQUIRED)
find_package(Eigen3 3.3 REQUIRED NO_MODULE)

if (NOT PYBIND_FORCE_PYTHON310)

find_package(Python3 REQUIRED COMPONENTS Interpreter Development)

else()

set(Python3_SITEARCH "/usr/local/lib/python3.10/dist-packages")
set(Python3_LIBRARIES "/usr/lib/x86_64-linux-gnu/libpython3.10.so")
set(Python3_INCLUDE_DIRS "/usr/include/python3.10")

endif()

find_package(pybind11 CONFIG REQUIRED)

add_library(pydsr MODULE)

target_sources(pydsr
        PRIVATE
        python_api.cpp
        )


target_link_libraries(pydsr
        PRIVATE
        Qt6::Core
        Eigen3::Eigen
        Robocomp::dsr_api
        Robocomp::dsr_core
        ${Python3_LIBRARIES}
        )

target_include_directories(pydsr
        PUBLIC
        # Headers of DSR Api and Core
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/../core/include/>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/../api/include/>
        # Own include
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include/>
        PRIVATE
        ${Python3_INCLUDE_DIRS}
        )


set_target_properties(pydsr PROPERTIES
        CMAKE_CXX_STANDARD 23
        CXX_STANDARD_REQUIRED ON
        CXX_EXTENSIONS ON
        Python_ADDITIONAL_VERSIONS 3.8)


# set (CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -fno-omit-frame-pointer -fsanitize=address")
# set (CMAKE_LINKER_FLAGS_DEBUG "${CMAKE_LINKER_FLAGS_DEBUG} -fno-omit-frame-pointer -fsanitize=address")

target_compile_options(pydsr PRIVATE -O3  -fmax-errors=5 -std=c++23)
set_target_properties(pydsr PROPERTIES PREFIX "")



# 'make install' to the correct locations (provided by GNUInstallDirs).
set(pydsr_INSTALL_PYTHONDIR "${Python3_SITEARCH}")
message(STATUS ${Python3_SITEARCH})
install(TARGETS pydsr DESTINATION ${pydsr_INSTALL_PYTHONDIR} COMPONENT module)

