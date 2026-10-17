# ===========================================================================================
# Specific Plugin CMake Configuration
# ===========================================================================================
# Project:      plugin test for Gluttony
# Location:     ${PROJECT_SOURCE_DIR}/plugin/PLUGIN_NAME/CMakeLists.txt
# Purpose:      compile the plugin in this die as a shared lib
# ===========================================================================================

include(FetchContent)
include(${CMAKE_SOURCE_DIR}/helper.cmake)
set(PLUGIN_NAME                                                         "physics_jolt")

file(GLOB_RECURSE PLUGIN_SOURCES
    CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cxx"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.c"
)
set_property(GLOBAL PROPERTY "${CMAKE_CURRENT_SOURCE_DIR}_SOURCES"      "${PLUGIN_SOURCES}")

# add libraries in both static and shared
compile_plugin(SHARED                                                   ${PLUGIN_NAME}          glm imgui)
compile_plugin(STATIC                                                   ${PLUGIN_NAME}_static   glm imgui)
target_compile_features(${PLUGIN_NAME}                                  PRIVATE cxx_std_23)
target_compile_features(${PLUGIN_NAME}_static                           PRIVATE cxx_std_23)

set(VENDOR_DIR                                                          "${CMAKE_SOURCE_DIR}/vendor")
set(JOLT_PHYSICS_DIR                                                    "${VENDOR_DIR}/JoltPhysics")



# Jolt Physics — fetch + build as a subproject

# Disable everything we don't need — MUST be set BEFORE FetchContent_MakeAvailable
set(TARGET_UNIT_TESTS                                                   OFF CACHE BOOL "" FORCE)
set(TARGET_HELLO_WORLD                                                  OFF CACHE BOOL "" FORCE)
set(TARGET_SAMPLES                                                      OFF CACHE BOOL "" FORCE)
set(TARGET_VIEWER                                                       OFF CACHE BOOL "" FORCE)
set(TARGET_PERFORMANCE_TEST                                             OFF CACHE BOOL "" FORCE)
set(INTERPROCEDURAL_OPTIMIZATION                                        ON CACHE BOOL "" FORCE)     # Performance toggles
set(USE_AVX2                                                            ON CACHE BOOL "" FORCE)

FetchContent_Declare(
    JoltPhysics
    GIT_REPOSITORY                                                      https://github.com/jrouwe/JoltPhysics.git
    GIT_TAG                                                             v5.5.0                    # pin a tag, never use main
    GIT_SHALLOW                                                         TRUE
    SOURCE_DIR                                                          "${JOLT_PHYSICS_DIR}"     # clone INTO your vendor dir
)
FetchContent_MakeAvailable(JoltPhysics)

# ---------------------------------------------------------------------------------------------
# Jolt Physics — vendored source
# ---------------------------------------------------------------------------------------------
# Jolt's CMake entry point is Build/CMakeLists.txt, NOT the repo root. FetchContent and a naive
# add_subdirectory(JOLT_PHYSICS_DIR) both fail silently for that reason.
#
# We clone on first configure and reuse thereafter. The check is self-healing: if the previous
# clone was interrupted (directory exists but Build/CMakeLists.txt is missing), we wipe and
# retry.

if (NOT EXISTS "${JOLT_PHYSICS_DIR}/Build/CMakeLists.txt")
    message(STATUS "[physics_jolt] Cloning JoltPhysics v5.5.0 into ${JOLT_PHYSICS_DIR}")
    file(REMOVE_RECURSE "${JOLT_PHYSICS_DIR}")
    execute_process(
        COMMAND          git clone --depth 1 --branch v5.5.0
                         https://github.com/jrouwe/JoltPhysics.git "${JOLT_PHYSICS_DIR}"
        RESULT_VARIABLE  _jolt_clone_result
        ERROR_VARIABLE   _jolt_clone_error
    )
    if (NOT _jolt_clone_result EQUAL 0 OR NOT EXISTS "${JOLT_PHYSICS_DIR}/Build/CMakeLists.txt")
        message(FATAL_ERROR
            "[physics_jolt] Failed to clone JoltPhysics into ${JOLT_PHYSICS_DIR}\n"
            "               git clone exited with ${_jolt_clone_result}\n"
            "               stderr:\n${_jolt_clone_error}")
    endif()
endif()

# Build options must be set before add_subdirectory processes Jolt's Build/CMakeLists.txt.
set(TARGET_UNIT_TESTS                                                   OFF CACHE BOOL "" FORCE)
set(TARGET_HELLO_WORLD                                                  OFF CACHE BOOL "" FORCE)
set(TARGET_SAMPLES                                                      OFF CACHE BOOL "" FORCE)
set(TARGET_VIEWER                                                       OFF CACHE BOOL "" FORCE)
set(TARGET_PERFORMANCE_TEST                                             OFF CACHE BOOL "" FORCE)
set(INTERPROCEDURAL_OPTIMIZATION                                        ON  CACHE BOOL "" FORCE)
set(USE_AVX2                                                            ON  CACHE BOOL "" FORCE)

# Note the /Build — this is the only path change from my earlier (wrong) suggestion.
add_subdirectory("${JOLT_PHYSICS_DIR}/Build" "${CMAKE_CURRENT_BINARY_DIR}/_jolt_build")

if (NOT TARGET Jolt)
    message(FATAL_ERROR "[physics_jolt] Jolt target not created — inspect ${JOLT_PHYSICS_DIR}/Build/CMakeLists.txt")
endif()

target_link_libraries(${PLUGIN_NAME}                                    PRIVATE Jolt)
target_link_libraries(${PLUGIN_NAME}_static                             PRIVATE Jolt)
target_include_directories(${PLUGIN_NAME}                               SYSTEM PRIVATE ${JOLT_PHYSICS_DIR})
target_include_directories(${PLUGIN_NAME}_static                        SYSTEM PRIVATE ${JOLT_PHYSICS_DIR})



# ------------- PCH header ------------------------------------------------------------------------
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/src/util/jolt_pch.h")
    target_precompile_headers(${PLUGIN_NAME}                            PRIVATE     # Enable precompiled headers globally for this target
        "${CMAKE_CURRENT_SOURCE_DIR}/src/util/jolt_pch.h"
    )
    target_compile_options(${PLUGIN_NAME}                               PRIVATE     # Force all files to use the PCH
        "$<$<CXX_COMPILER_ID:MSVC>:/FI${CMAKE_CURRENT_SOURCE_DIR}/src/util/jolt_pch.h>"
        "$<$<CXX_COMPILER_ID:GNU>:-include${CMAKE_CURRENT_SOURCE_DIR}/src/util/jolt_pch.h>"
        "$<$<CXX_COMPILER_ID:Clang>:-include${CMAKE_CURRENT_SOURCE_DIR}/src/util/jolt_pch.h>"
    )
endif()



print_source_file_details(
    SOURCES                                                             ${PLUGIN_SOURCES}
    NAME                                                                ${PLUGIN_NAME}
    SOURCE_DIR                                                          "${CMAKE_CURRENT_SOURCE_DIR}"
    VERBOSE                                                             CORE_VERBOSE_INFO
    TARGET                                                              ${PLUGIN_NAME}
)
