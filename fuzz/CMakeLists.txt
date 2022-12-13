cmake_minimum_required(VERSION 3.10)

project(dsr_fuzz
        VERSION 0.1.0
        DESCRIPTION "Robocomp DSR fuzzing"
        LANGUAGES CXX )

find_package(Qt5 COMPONENTS Core REQUIRED)
find_package(Eigen3 3.3 REQUIRED NO_MODULE)


file(GLOB files "src/*.cpp")

foreach(file ${files})
   get_filename_component(file_no_ext ${file} NAME_WE)  # should be NAME_WLE if newer cmake

   add_executable(${file_no_ext} ${file})
   target_link_libraries(${file_no_ext} PRIVATE deepstate pthread Qt5::Core dsr_core dsr_gui dsr_api fastcdr fastrtps osgDB OpenThreads Eigen3::Eigen)
   
   # For now support for basic fuzzing is enough
   #if (DEEPSTATE_LIBFUZZER)
   #   add_executable("${file_no_ext}_LF" "${file}")
   #   target_link_libraries("${file_no_ext}_LF" deepstate_LF)
   #   target_link_libraries("${file_no_ext}_LF" "-fsanitize=fuzzer,undefined")
   #   set_target_properties("${file_no_ext}_LF" PROPERTIES COMPILE_DEFINITIONS "LIBFUZZER")
   #endif()

   #if (DEEPSTATE_AFL)
   #   add_executable("${file_no_ext}_AFL" "${file}")
   #   target_link_libraries("${file_no_ext}_AFL" deepstate_AFL)
   #endif()


   target_include_directories(${file_no_ext}
   PRIVATE
   # TODO: Don't like, try to fix
   $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/../../../classes/>
   PUBLIC
   # Own headers
   $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include/>
   $<INSTALL_INTERFACE:>
   )

   set_target_properties(${file_no_ext} PROPERTIES
   CMAKE_CXX_STANDARD 20
   CXX_STANDARD_REQUIRED ON
   CXX_EXTENSIONS ON)

   target_compile_options(${file_no_ext} PUBLIC -O1 -g -fmax-errors=5 -std=c++20 -fno-char8_t)


   install(FILES ${EXECUTABLE_OUTPUT_PATH}/${file_no_ext} DESTINATION ${RC_COMPONENT_INSTALL_PATH}/bin/ PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE GROUP_READ GROUP_EXECUTE WORLD_READ WORLD_EXECUTE )

endforeach()


# https://github.com/trailofbits/deepstate/blob/master/docs/basic_usage.md