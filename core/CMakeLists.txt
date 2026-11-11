# =================================================================================================
# Root CMake Configuration
# =================================================================================================
# Project:      plugin test for Gluttony
# Location:     ${PROJECT_SOURCE_DIR}/core/CMakeLists.txt
# Purpose:      compile the core of the application (microkernel)
# =================================================================================================

SET(CORE_NAME                                                           "Gluttony")

file(GLOB_RECURSE                                                       CORE_SOURCES
    CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cxx"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.c"
)
add_executable(${CORE_NAME}                                             ${CORE_SOURCES})
set_target_properties(${CORE_NAME} PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY                                            "${COMPILE_OUTPUT_DIR}"
    CXX_VISIBILITY_PRESET                                               default
    VISIBILITY_INLINES_HIDDEN                                           OFF
    JOB_POOL_LINK                                                       link_pool
)

target_compile_definitions(${CORE_NAME}                                 PRIVATE
    GLT_MODULE_NAME="${CORE_NAME}"
    $<$<CONFIG:Debug>:DEBUG>
    $<$<CONFIG:Release>:RELEASE>
    $<$<CONFIG:RelWithDebInfo>:RELEASE_WITH_DEBUG_INFO>
    IMGUI_DEFINE_MATH_OPERATORS
    USE_IMGUI
)
target_include_directories(${CORE_NAME}                                 PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src/
    ${CMAKE_SOURCE_DIR}/vendor/glm
)

set(COMPILE_OUTPUT_DIR                                                  "${CMAKE_BINARY_DIR}/bin")
set_target_properties(${CORE_NAME} PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY                                            "${COMPILE_OUTPUT_DIR}"
    CXX_VISIBILITY_PRESET                                               default
    VISIBILITY_INLINES_HIDDEN                                           OFF
    JOB_POOL_LINK                                                       link_pool
)
if(GCC16_RUNTIME_DIR)
    set_target_properties(${CORE_NAME} PROPERTIES
        BUILD_RPATH                                                     "${GCC16_RUNTIME_DIR}"
        INSTALL_RPATH                                                   "${GCC16_RUNTIME_DIR}"
    )
endif()

# For the main executable target (e.g., "Gluttony")
target_link_options(${CORE_NAME}                                        PRIVATE
    -rdynamic
    -static-libstdc++
    -static-libgcc
)
target_link_libraries(${CORE_NAME}                                      PRIVATE
    glm
    imgui
    meshoptimizer                                                       # asset/mesh_lod.cpp (exported to plugins via -rdynamic)
)
if(TARGET tracy_client)
    target_link_libraries(${CORE_NAME}                                  PRIVATE tracy_client)
endif()

# expose API for gluttony user projects -----------------------------------------------------------
add_library(${CORE_NAME}API INTERFACE)
add_library(${CORE_NAME}::API ALIAS ${CORE_NAME}API)

# -------------------------------------------------------------------------------------------------
# NOTE ON imgui AND glm
# -------------------------------------------------------------------------------------------------
# We deliberately do NOT call `target_link_libraries(${CORE_NAME}API INTERFACE imgui)`.
# Reasons:
#   1. imgui is a SHARED library loaded by the host executable. At runtime, any plugin
#      dlopen()'d into the host process can resolve imgui symbols against the host's
#      already-loaded libimgui.so — no link dependency needed in the plugin.
#   2. Exporting imgui via install(EXPORT ...) drags in its transitive deps (glfw, Vulkan),
#      which then all need their own export/install rules. This is fragile and unnecessary
#      for a plugin model.
#   3. Bakes absolute source paths into the exported target when `target_sources` is PUBLIC.
#
# The consumer still gets imgui headers below (via include dirs) so they can `#include
# <imgui.h>` and call imgui functions; only the link-time dependency is dropped, and that's
# fine because MODULE libraries (Linux) allow undefined symbols by default.
# -------------------------------------------------------------------------------------------------

target_include_directories(${CORE_NAME}API
    INTERFACE
        # Core headers
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}/Gluttony>
        # imgui headers (flat layout: <imgui.h>, <backends/imgui_impl_vulkan.h>)
        $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/vendor/imgui>
        $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/vendor/imgui/backends>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}/backends>
        # glm headers (so public headers using glm::vec3, glm::mat4 etc. work)
        $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/vendor/glm>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)

target_compile_definitions(${CORE_NAME}API INTERFACE
    $<$<CONFIG:Debug>:DEBUG>
    $<$<CONFIG:Release>:RELEASE>
    $<$<CONFIG:RelWithDebInfo>:RELEASE_WITH_DEBUG_INFO>
    IMGUI_DEFINE_MATH_OPERATORS
    USE_IMGUI
)

target_compile_options(${CORE_NAME}API INTERFACE
    $<$<COMPILE_LANGUAGE:CXX>:-std=c++26>
    $<$<COMPILE_LANGUAGE:CXX>:-freflection>
    $<$<COMPILE_LANGUAGE:CXX>:-ftemplate-depth=512>
    $<$<COMPILE_LANGUAGE:CXX>:-fdiagnostics-color=always>
    $<$<AND:$<COMPILE_LANGUAGE:CXX>,$<CONFIG:Debug>>:-g3>
)

install(TARGETS ${CORE_NAME}API EXPORT GluttonyTargets)
install(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/src/
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/Gluttony
    FILES_MATCHING PATTERN "*.h" PATTERN "*.hpp" PATTERN "*.inl"
)



# ------------- PCH header ------------------------------------------------------------------------
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/src/util/pch.h")
    target_precompile_headers(${CORE_NAME}                              PRIVATE     # Enable precompiled headers globally for this target
        "${CMAKE_CURRENT_SOURCE_DIR}/src/util/pch.h"
    )
    target_compile_options(${CORE_NAME}                                 PRIVATE     # Force all files to use the PCH
        "$<$<CXX_COMPILER_ID:MSVC>:/FI${CMAKE_CURRENT_SOURCE_DIR}/src/util/pch.h>"
        "$<$<CXX_COMPILER_ID:GNU>:-include${CMAKE_CURRENT_SOURCE_DIR}/src/util/pch.h>"
        "$<$<CXX_COMPILER_ID:Clang>:-include${CMAKE_CURRENT_SOURCE_DIR}/src/util/pch.h>"
    )
endif()

target_compile_options(${CORE_NAME}                                     PRIVATE
    $<$<COMPILE_LANGUAGE:CXX>:-std=c++26>
    $<$<COMPILE_LANGUAGE:CXX>:-freflection>
    $<$<COMPILE_LANGUAGE:CXX>:-ftemplate-depth=512>
    $<$<AND:$<COMPILE_LANGUAGE:CXX>,$<CONFIG:Debug>>:-g3>
    $<$<COMPILE_LANGUAGE:CXX>:-fdiagnostics-color=always>
)



# ------------- Copy assets directory to output ---------------------------------------------------
if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/asset)
    add_custom_target(${CORE_NAME}_copy_assets ALL
        COMMAND                                                         ${CMAKE_COMMAND} -E copy_directory
                ${CMAKE_CURRENT_SOURCE_DIR}/asset
                ${COMPILE_OUTPUT_DIR}/asset
        COMMENT "Copying assets from component [${CORE_NAME}] to build directory: ${COMPILE_OUTPUT_DIR}/asset"
        VERBATIM
    )
    # Make sure assets are only copied after the executable is linked
    add_dependencies(${CORE_NAME}_copy_assets ${CORE_NAME})
else()
    message(WARNING "Assets directory not found at ${CMAKE_SOURCE_DIR}/asset")
endif()



# ------------- Clone wiki to output directory ----------------------------------------------------
set(WIKI_URL                                                            "git@github.com:Mich-Dich/Gluttony.wiki.git")
set(WIKI_DIR                                                            "${COMPILE_OUTPUT_DIR}/wiki")

add_custom_command(
    OUTPUT                                                              ${WIKI_DIR}/.git
    COMMAND                                                             sh -c "if [ -d ${WIKI_DIR}/.git ]; then git -C ${WIKI_DIR} pull; else git clone --depth 1 ${WIKI_URL} ${WIKI_DIR}; fi"
    COMMENT                                                             "Cloning/updating wiki: ${WIKI_DIR}"
    VERBATIM
)

add_custom_target(${CORE_NAME}_clone_wiki
    DEPENDS                                                             ${WIKI_DIR}/.git
)
add_dependencies(${CORE_NAME}_clone_wiki ${CORE_NAME})



include(${CMAKE_SOURCE_DIR}/helper.cmake)
print_source_file_details(
    SOURCES                                                             ${CORE_SOURCES}
    NAME                                                                ${CORE_NAME}
    SOURCE_DIR                                                          "${CMAKE_CURRENT_SOURCE_DIR}"
    VERBOSE                                                             CORE_VERBOSE_INFO
    TARGET                                                              ${CORE_NAME}
)
