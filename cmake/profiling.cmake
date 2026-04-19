# Profiling backend selection for the cortex library.
# Defines and populates the cortex_profiling INTERFACE target, which consumers
# link against to get the right compile definitions and libraries.
#
# Usage:
#   cmake -DCORTEX_PROFILING_BACKEND=TRACY -DCORTEX_ENABLE_TRACY=WSL_EXTRA ..
#   cmake -DCORTEX_PROFILING_BACKEND=PERFETTO ..

set(CORTEX_PROFILING_BACKEND "NONE" CACHE STRING
    "Profiling backend. Accepted values: NONE, TRACY, PERFETTO")
set(CORTEX_CALLSTACK_DEPTH "16" CACHE STRING
    "Number of stack frames captured by CORTEX_PROFILE_ZONE_CS")
set_property(CACHE CORTEX_PROFILING_BACKEND PROPERTY STRINGS NONE TRACY PERFETTO)
string(TOUPPER "${CORTEX_PROFILING_BACKEND}" CORTEX_PROFILING_BACKEND_MODE)

add_library(cortex_profiling INTERFACE)

# ── Tracy ─────────────────────────────────────────────────────────────────────

set(CORTEX_ENABLE_TRACY "OFF" CACHE STRING
    "Tracy preset. Accepted values: OFF, ON, DEFAULT, MEDIUM, EXTRA, WSL, WSL_EXTRA")
set_property(CACHE CORTEX_ENABLE_TRACY PROPERTY STRINGS OFF ON DEFAULT MEDIUM EXTRA WSL WSL_EXTRA)
option(CORTEX_TRACY_ON_DEMAND  "Enable Tracy on-demand connection mode"      ON)
option(CORTEX_TRACY_CALLSTACK  "Enable Tracy callstack capture"               OFF)
option(CORTEX_TRACY_ONLY_LOCALHOST "Restrict Tracy connections to localhost"  ON)
option(CORTEX_TRACY_NO_BROADCAST   "Disable Tracy client broadcast discovery" ON)
string(TOUPPER "${CORTEX_ENABLE_TRACY}" CORTEX_TRACY_MODE)
set(CORTEX_TRACY_ENABLED OFF)

# Allow CORTEX_ENABLE_TRACY to implicitly select the Tracy backend.
if (CORTEX_PROFILING_BACKEND_MODE STREQUAL "NONE")
    if (NOT CORTEX_TRACY_MODE STREQUAL "OFF" AND
        NOT CORTEX_TRACY_MODE STREQUAL "FALSE" AND
        NOT CORTEX_TRACY_MODE STREQUAL "NO" AND
        NOT CORTEX_TRACY_MODE STREQUAL "0")
        set(CORTEX_PROFILING_BACKEND_MODE "TRACY")
    endif()
endif()

# If TRACY backend is explicit but CORTEX_ENABLE_TRACY was left at OFF, use DEFAULT preset.
if (CORTEX_PROFILING_BACKEND_MODE STREQUAL "TRACY" AND
    (CORTEX_TRACY_MODE STREQUAL "OFF" OR
     CORTEX_TRACY_MODE STREQUAL "FALSE" OR
     CORTEX_TRACY_MODE STREQUAL "NO" OR
     CORTEX_TRACY_MODE STREQUAL "0"))
    set(CORTEX_ENABLE_TRACY "DEFAULT")
    set(CORTEX_TRACY_MODE "DEFAULT")
endif()

# Preset table: each preset sets the four Tracy knobs.
if (CORTEX_TRACY_MODE STREQUAL "OFF" OR
    CORTEX_TRACY_MODE STREQUAL "FALSE" OR
    CORTEX_TRACY_MODE STREQUAL "NO" OR
    CORTEX_TRACY_MODE STREQUAL "0")
    set(CORTEX_TRACY_ENABLED OFF)
elseif (CORTEX_TRACY_MODE STREQUAL "ON" OR CORTEX_TRACY_MODE STREQUAL "DEFAULT")
    set(CORTEX_TRACY_ENABLED ON)
    set(CORTEX_TRACY_ON_DEMAND ON)
    set(CORTEX_TRACY_CALLSTACK OFF)
    set(CORTEX_TRACY_ONLY_LOCALHOST ON)
    set(CORTEX_TRACY_NO_BROADCAST ON)
elseif (CORTEX_TRACY_MODE STREQUAL "MEDIUM")
    set(CORTEX_TRACY_ENABLED ON)
    set(CORTEX_TRACY_ON_DEMAND OFF)
    set(CORTEX_TRACY_CALLSTACK OFF)
    set(CORTEX_TRACY_ONLY_LOCALHOST ON)
    set(CORTEX_TRACY_NO_BROADCAST ON)
elseif (CORTEX_TRACY_MODE STREQUAL "EXTRA")
    set(CORTEX_TRACY_ENABLED ON)
    set(CORTEX_TRACY_ON_DEMAND OFF)
    set(CORTEX_TRACY_CALLSTACK ON)
    set(CORTEX_TRACY_ONLY_LOCALHOST ON)
    set(CORTEX_TRACY_NO_BROADCAST ON)
elseif (CORTEX_TRACY_MODE STREQUAL "WSL")
    set(CORTEX_TRACY_ENABLED ON)
    set(CORTEX_TRACY_ON_DEMAND OFF)
    set(CORTEX_TRACY_CALLSTACK OFF)
    set(CORTEX_TRACY_ONLY_LOCALHOST OFF)
    set(CORTEX_TRACY_NO_BROADCAST OFF)
elseif (CORTEX_TRACY_MODE STREQUAL "WSL_EXTRA")
    set(CORTEX_TRACY_ENABLED ON)
    set(CORTEX_TRACY_ON_DEMAND OFF)
    set(CORTEX_TRACY_CALLSTACK ON)
    set(CORTEX_TRACY_ONLY_LOCALHOST OFF)
    set(CORTEX_TRACY_NO_BROADCAST OFF)
else()
    message(FATAL_ERROR
        "Invalid value for CORTEX_ENABLE_TRACY='${CORTEX_ENABLE_TRACY}'. "
        "Use one of: OFF, ON, DEFAULT, MEDIUM, EXTRA, WSL, WSL_EXTRA")
endif()

if (CORTEX_PROFILING_BACKEND_MODE STREQUAL "TRACY" AND CORTEX_TRACY_ENABLED)
    message(STATUS "Tracy support enabled with preset: ${CORTEX_TRACY_MODE}")

    set(CORTEX_TRACY_ON_DEMAND      ${CORTEX_TRACY_ON_DEMAND}      CACHE BOOL "" FORCE)
    set(CORTEX_TRACY_CALLSTACK      ${CORTEX_TRACY_CALLSTACK}      CACHE BOOL "" FORCE)
    set(CORTEX_TRACY_ONLY_LOCALHOST ${CORTEX_TRACY_ONLY_LOCALHOST} CACHE BOOL "" FORCE)
    set(CORTEX_TRACY_NO_BROADCAST   ${CORTEX_TRACY_NO_BROADCAST}   CACHE BOOL "" FORCE)

    set(TRACY_ON_DEMAND      ${CORTEX_TRACY_ON_DEMAND}      CACHE BOOL "" FORCE)
    set(TRACY_CALLSTACK      ${CORTEX_TRACY_CALLSTACK}      CACHE BOOL "" FORCE)
    set(TRACY_ONLY_LOCALHOST ${CORTEX_TRACY_ONLY_LOCALHOST} CACHE BOOL "" FORCE)
    set(TRACY_NO_BROADCAST   ${CORTEX_TRACY_NO_BROADCAST}   CACHE BOOL "" FORCE)

    FetchContent_Declare(
        tracy
        GIT_REPOSITORY https://github.com/wolfpld/tracy.git
        GIT_TAG        v0.11.1
    )
    FetchContent_MakeAvailable(tracy)

    if (TARGET Tracy::TracyClient)
        if (TARGET TracyClient)
            set_property(TARGET TracyClient PROPERTY POSITION_INDEPENDENT_CODE ON)
        endif()
        target_link_libraries(cortex_profiling INTERFACE Tracy::TracyClient)
    elseif (TARGET TracyClient)
        set_property(TARGET TracyClient PROPERTY POSITION_INDEPENDENT_CODE ON)
        target_link_libraries(cortex_profiling INTERFACE TracyClient)
    else()
        message(FATAL_ERROR "Tracy target not found after FetchContent_MakeAvailable(tracy)")
    endif()

    target_compile_definitions(cortex_profiling INTERFACE
        TRACY_ENABLE CORTEX_PROFILING_BACKEND_TRACY
        CORTEX_CALLSTACK_DEPTH=${CORTEX_CALLSTACK_DEPTH})

    message(STATUS "  on-demand:  ${CORTEX_TRACY_ON_DEMAND}")
    message(STATUS "  callstack:  ${CORTEX_TRACY_CALLSTACK}")
    message(STATUS "  localhost:  ${CORTEX_TRACY_ONLY_LOCALHOST}")
    message(STATUS "  broadcast:  ${CORTEX_TRACY_NO_BROADCAST}")

# ── Perfetto ──────────────────────────────────────────────────────────────────

elseif (CORTEX_PROFILING_BACKEND_MODE STREQUAL "PERFETTO")
    message(STATUS "Perfetto support enabled")

    FetchContent_Declare(
        perfetto_sdk_src
        URL https://github.com/google/perfetto/releases/download/v54.0/perfetto-cpp-sdk-src.zip
    )
    FetchContent_MakeAvailable(perfetto_sdk_src)

    add_library(perfetto_sdk STATIC
        ${perfetto_sdk_src_SOURCE_DIR}/perfetto.cc
    )
    set_property(TARGET perfetto_sdk PROPERTY POSITION_INDEPENDENT_CODE ON)
    target_include_directories(perfetto_sdk PUBLIC ${perfetto_sdk_src_SOURCE_DIR})
    target_link_libraries(perfetto_sdk PUBLIC Threads::Threads)

    target_link_libraries(cortex_profiling INTERFACE perfetto_sdk dl)
    target_compile_definitions(cortex_profiling INTERFACE
        CORTEX_PROFILING_BACKEND_PERFETTO
        CORTEX_CALLSTACK_DEPTH=${CORTEX_CALLSTACK_DEPTH})

elseif (NOT CORTEX_PROFILING_BACKEND_MODE STREQUAL "NONE")
    message(FATAL_ERROR
        "Invalid value for CORTEX_PROFILING_BACKEND='${CORTEX_PROFILING_BACKEND}'. "
        "Use one of: NONE, TRACY, PERFETTO")
endif()
