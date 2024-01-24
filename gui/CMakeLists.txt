cmake_minimum_required( VERSION 3.10 )
project(dsr_gui
        VERSION 2021.3.1
        DESCRIPTION "Robocomp DSR gui library"
        LANGUAGES CXX )

option(QT3D "Compile QT3D viewer WIP" OFF)

if(QT3D)

set(qt3d_required
        3DCore 3DRender 3DExtras 3DInput 3DAnimation
        )

set(qt3d_viewer_headers   
        include/dsr/gui/viewers/qt3d_viewer/qt3d_viewer.h
        )

set(qt3d_viewer_sources  
        viewers/qt3d_viewer/qt3d_viewer.cpp
        )

set (qt3d_libs 
        Qt6::3DCore
        Qt6::3DRender
        Qt6::3DAnimation
        Qt6::3DExtras
        Qt6::3DInput
        )

endif()


message(STATUS ${qt3d_required} " " ${qt3d_viewer_headers} " " ${qt3d_libs} " " ${qt3d_viewer_sources})

find_package(Qt6 COMPONENTS Widgets OpenGL Xml ${qt3d_required} REQUIRED)
find_package(Eigen3 3.3 REQUIRED NO_MODULE)
find_package(OpenSceneGraph REQUIRED COMPONENTS osg osgDB osgGA)
find_package(cppitertools)

SET(CMAKE_AUTOMOC ON)
SET(CMAKE_AUTOUIC ON)

# TODO: look for a better way to get automoc getting over .h files
set(headers_to_moc
        include/dsr/gui/viewers/osg_3d_viewer/osg_3d_viewer.h
        include/dsr/gui/viewers/qscene_2d_viewer/qscene_2d_viewer.h
        include/dsr/gui/viewers/graph_viewer/graph_viewer.h
        include/dsr/gui/viewers/graph_viewer/graph_node.h
        include/dsr/gui/viewers/graph_viewer/graph_edge.h
        include/dsr/gui/viewers/graph_viewer/graph_edge_widget.h
        include/dsr/gui/viewers/graph_viewer/graph_edge_rt_widget.h
        include/dsr/gui/viewers/graph_viewer/graph_node_laser_widget.h
        include/dsr/gui/viewers/graph_viewer/graph_node_rgbd_widget.h
        include/dsr/gui/viewers/graph_viewer/graph_node_person_widget.h
        include/dsr/gui/viewers/graph_viewer/graph_node_widget.h
        include/dsr/gui/viewers/tree_viewer/tree_viewer.h
        include/dsr/gui/viewers/_abstract_graphic_view.h
        include/dsr/gui/dsr_gui.h
        ${qt3d_viewer_headers}
        )

add_library(dsr_gui SHARED )
add_library(Robocomp::dsr_gui ALIAS dsr_gui )

target_sources(dsr_gui
        PRIVATE
        dsr_gui.cpp
        viewers/osg_3d_viewer/osg_3d_viewer.cpp
        viewers/qscene_2d_viewer/qscene_2d_viewer.cpp
        viewers/graph_viewer/graph_viewer.cpp
        viewers/graph_viewer/graph_node.cpp
        viewers/graph_viewer/graph_edge.cpp
        viewers/tree_viewer/tree_viewer.cpp
        viewers/_abstract_graphic_view.cpp
        ${qt3d_viewer_sources}
        ${headers_to_moc}
        )


target_link_libraries(dsr_gui
        PRIVATE
        Qt6::Widgets Qt6::Xml
        Eigen3::Eigen
        fastcdr
        osgViewer osgDB osgGA OpenThreads
        PUBLIC
        Qt6::OpenGL
        fastrtps
        ${qt3d_libs}
        )

target_include_directories(dsr_gui
        PRIVATE
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/../core/include/>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/../api/include/>
        # TODO: Don't like, try to fix
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/../../../classes/>
        PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include/>
        $<INSTALL_INTERFACE:>
        )

set_target_properties(dsr_gui
        PROPERTIES
        CMAKE_CXX_STANDARD 20
        CXX_STANDARD_REQUIRED ON
        CXX_EXTENSIONS ON)

target_compile_options(dsr_gui PUBLIC -O3 -g -fmax-errors=5 -std=c++20 -fno-char8_t )

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
