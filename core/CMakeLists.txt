cmake_minimum_required( VERSION 3.10 )
project( dsr_core
        VERSION 1.0.0
        DESCRIPTION "Robocomp DSR core library"
        LANGUAGES CXX )


find_package(Qt6 COMPONENTS Core REQUIRED)
find_package(Eigen3 3.3 REQUIRED NO_MODULE)

add_library(dsr_core SHARED)
add_library(Robocomp::dsr_core ALIAS dsr_core)

target_sources(dsr_core
        PRIVATE
        types/crdt_types.cpp
        types/user_types.cpp
        types/common_types.cpp
        include/dsr/core/types/crdt_types.h
        include/dsr/core/types/user_types.h
        include/dsr/core/types/common_types.h
        include/dsr/core/types/translator.h
        include/dsr/core/types/type_checking/dsr_attr_name.h
        include/dsr/core/types/type_checking/dsr_edge_type.h
        include/dsr/core/types/type_checking/dsr_node_type.h
        include/dsr/core/types/type_checking/type_checker.h

        rtps/dsrpublisher.cpp
        rtps/dsrsubscriber.cpp
        rtps/dsrparticipant.cpp
        include/dsr/core/rtps/dsrparticipant.h
        include/dsr/core/rtps/dsrpublisher.h
        include/dsr/core/rtps/dsrsubscriber.h

        topics/IDLGraphPubSubTypes.cxx
        topics/IDLGraph.cxx
        topics/IDLGraphCdrAux.ipp
        include/dsr/core/topics/IDLGraph.h
        include/dsr/core/topics/IDLGraphPubSubTypes.h
        include/dsr/core/topics/IDLGraphCdrAux.hpp

        include/dsr/core/crdt/delta_crdt.h

        include/dsr/core/id_generator.h
        id_generator.cpp

        include/dsr/core/traits.h
        include/dsr/core/utils.h
        )


target_link_libraries(dsr_core
        PRIVATE
        Qt6::Core fastcdr fastrtps osgDB OpenThreads Eigen3::Eigen)


target_include_directories(dsr_core
        PRIVATE
        # TODO: Don't like, try to fix
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/../../../classes/>
        PUBLIC
        # Own headers
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include/>
        $<INSTALL_INTERFACE:>
        )

set_target_properties(dsr_core PROPERTIES
        CMAKE_CXX_STANDARD 20
        CXX_STANDARD_REQUIRED ON
        CXX_EXTENSIONS ON)

target_compile_options(dsr_core PUBLIC -O3 -fmax-errors=5 -std=c++20 -fno-char8_t
	)

# 'make install' to the correct locations (provided by GNUInstallDirs).
install(TARGETS dsr_core EXPORT dsr_coreConfig
        ARCHIVE  DESTINATION ${CMAKE_INSTALL_LIBDIR}
        LIBRARY  DESTINATION ${CMAKE_INSTALL_LIBDIR}
        )

install(DIRECTORY include/dsr DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})


# This makes the project importable from the install directory
# Put config file in per-project dir (name MUST match), can also
# just go into 'cmake'.
install(EXPORT dsr_coreConfig
        DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/dsr_core/
        NAMESPACE Robocomp::
        )

# This makes the project importable from the build directory
export(TARGETS dsr_core FILE dsr_coreConfig.cmake)
