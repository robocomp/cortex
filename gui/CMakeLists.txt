cmake_minimum_required( VERSION 3.10 )
project(dsr_gui
        VERSION 2021.3.1
        DESCRIPTION "Robocomp DSR gui library"
        LANGUAGES CXX )

find_package(Qt6 COMPONENTS Widgets OpenGL OpenGLWidgets Xml REQUIRED)
find_package(Eigen3 3.3 REQUIRED NO_MODULE)
find_package(cppitertools)

find_package(PkgConfig)
pkg_check_modules(GRAPHVIZ libgvc libcgraph)
if(GRAPHVIZ_FOUND)
    message(STATUS "Found Graphviz via pkg-config")
    #message(STATUS "  Include dirs: ${GRAPHVIZ_INCLUDE_DIRS}")
    #message(STATUS "  Libraries: ${GRAPHVIZ_LIBRARIES}")
    #message(STATUS "  Library dirs: ${GRAPHVIZ_LIBRARY_DIRS}")
endif()


SET(CMAKE_AUTOMOC ON)
SET(CMAKE_AUTOUIC ON)

# TODO: look for a better way to get automoc getting over .h files
set(headers_to_moc
        include/dsr/gui/viewers/qscene_2d_viewer/qscene_2d_viewer.h
        include/dsr/gui/viewers/graph_viewer/graph_viewer.h
        include/dsr/gui/viewers/graph_viewer/graph_node.h
        include/dsr/gui/viewers/graph_viewer/graph_edge.h
        include/dsr/gui/viewers/graph_viewer/graph_edge_widget.h
        include/dsr/gui/viewers/graph_viewer/graph_edge_rt_widget.h
        include/dsr/gui/viewers/graph_viewer/graph_node_imu_widget.h
        include/dsr/gui/viewers/graph_viewer/graph_node_laser_widget.h
        include/dsr/gui/viewers/graph_viewer/graph_node_rgbd_widget.h
        include/dsr/gui/viewers/graph_viewer/graph_node_person_widget.h
        include/dsr/gui/viewers/graph_viewer/graph_node_mind_widget.h
        include/dsr/gui/viewers/graph_viewer/graph_node_widget.h
        include/dsr/gui/viewers/tree_viewer/tree_viewer.h
        include/dsr/gui/viewers/_abstract_graphic_view.h
        include/dsr/gui/dsr_gui.h
        )

add_library(dsr_gui SHARED )
add_library(Robocomp::dsr_gui ALIAS dsr_gui )

target_sources(dsr_gui
        PRIVATE
        dsr_gui.cpp
        viewers/qscene_2d_viewer/qscene_2d_viewer.cpp
        viewers/graph_viewer/graph_viewer.cpp
        viewers/graph_viewer/graph_node.cpp
        viewers/graph_viewer/graph_edge.cpp
        viewers/tree_viewer/tree_viewer.cpp
        viewers/_abstract_graphic_view.cpp
        ${headers_to_moc}
        )


target_link_libraries(dsr_gui
        PRIVATE
        Qt6::Widgets Qt6::Xml Qt6::OpenGLWidgets
        Eigen3::Eigen
        fastcdr
        PUBLIC
        Qt6::OpenGL
        fastdds
        ${GRAPHVIZ_LIBRARIES}
        )

target_include_directories(dsr_gui
        PRIVATE
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/../core/include/>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/../api/include/>
        PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include/>
        $<INSTALL_INTERFACE:>
        ${GRAPHVIZ_INCLUDE_DIRS}
        )

set_target_properties(dsr_gui
        PROPERTIES
        CMAKE_CXX_STANDARD 20
        CXX_STANDARD_REQUIRED ON
        CXX_EXTENSIONS ON)

target_compile_options(dsr_gui PUBLIC -O3 -g -fmax-errors=5 -std=c++20 -fno-char8_t )

# Fast DDS Statistics consumer for the mind view (opt-in, dev/debug only). OFF by default so normal
# and DEPLOYMENT builds contain ZERO statistics code. eProsima installs the statistics *type* headers
# only as .idl, so enabling this also needs a Fast-DDS SOURCE tree to include the generated types from:
#   cmake -B build -DRC_DDS_STATS=ON -DRC_DDS_STATS_SRC=/path/to/Fast-DDS/src/cpp
# At runtime it is still inert unless the env var RC_DDS_STATS=1 is set (a second, no-rebuild gate).
option(RC_DDS_STATS "Build the Fast DDS Statistics consumer into the mind view (dev only)" OFF)
if(RC_DDS_STATS)
  if(NOT RC_DDS_STATS_SRC OR NOT EXISTS "${RC_DDS_STATS_SRC}/statistics/types/typesPubSubTypes.hpp")
    message(FATAL_ERROR "RC_DDS_STATS=ON requires -DRC_DDS_STATS_SRC=<Fast-DDS>/src/cpp (statistics type headers)")
  endif()
  message(STATUS "DDS Statistics consumer ENABLED for dsr_gui (types from ${RC_DDS_STATS_SRC})")
  target_compile_definitions(dsr_gui PRIVATE RC_DDS_STATS=1)
  target_include_directories(dsr_gui PRIVATE $<BUILD_INTERFACE:${RC_DDS_STATS_SRC}>)
endif()

# AddressSanitizer (opt-in): cmake -B build-asan -DENABLE_ASAN=ON
# Only needed for redzone-precise detection INSIDE the graph viewer. Do NOT `make install`
# an ASan build to /usr/local/lib — it would break every non-ASan agent. Build it locally and
# point the ASan voxelizer at it via LD_LIBRARY_PATH.
option(ENABLE_ASAN "Build dsr_gui with AddressSanitizer" OFF)
if(ENABLE_ASAN)
  message(STATUS "AddressSanitizer ENABLED for dsr_gui")
  target_compile_options(dsr_gui PUBLIC -fsanitize=address -fno-omit-frame-pointer -O1)
  target_link_options(dsr_gui PUBLIC -fsanitize=address)
endif()

# 'make install' to the correct locations (provided by GNUInstallDirs).
install(TARGETS dsr_gui EXPORT dsr_guiConfig
        ARCHIVE  DESTINATION ${CMAKE_INSTALL_LIBDIR}
        LIBRARY  DESTINATION ${CMAKE_INSTALL_LIBDIR}
        )

install(DIRECTORY include/dsr DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})

# This makes the project importable from the install directory
# Put config file in per-project dir (name MUST match), can also
# just go into 'cmake'.
install(EXPORT dsr_guiConfig DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/dsr_gui/ NAMESPACE Robocomp::)


# This makes the project importable from the build directory
export(TARGETS dsr_gui FILE dsr_guiConfig.cmake)
