# =================================================================================================
# Launcher CMake Configuration
# =================================================================================================
# Project:      Gluttony Launcher (standalone)
# Location:     ${PROJECT_SOURCE_DIR}/launcher/CMakeLists.txt
# Purpose:      standalone launcher for the engine (ImGui-only, does NOT link against core)
#
# This project is intentionally self-contained: it can be configured and built on its own,
# without touching the engine. The engine's root CMakeLists.txt does NOT add this directory,
# so building the engine never pulls in the launcher, and vice versa.
#
# Usage (from launcher/ directory):
#   cmake --preset debug
#   cmake --build --preset debug
#
# Or directly:
#   cmake -S . -B build/debug -G Ninja -DCMAKE_BUILD_TYPE=Debug
#   cmake --build build/debug
# =================================================================================================

cmake_minimum_required(VERSION                                          3.24)
project(gluttony_launcher VERSION 0.1.0                                 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD                                                  26)
set(CMAKE_CXX_STANDARD_REQUIRED                                         ON)
set(CMAKE_CXX_EXTENSIONS                                                OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS                                       ON)
set(COMPILE_OUTPUT_DIR                                                  "${CMAKE_BINARY_DIR}/bin")


if(UNIX AND NOT APPLE)
    add_compile_definitions(PLATFORM_LINUX)
    set(LINUX_SYSTEM_LIBS                                               pthread dl) # set a variable for system libraries
elseif(WIN32)
    add_compile_definitions(PLATFORM_WINDOWS)
    if(MSVC)                                                            # Apply /bigobj to all targets (valid only for MSVC)
        add_compile_options(/bigobj)
    endif()
elseif(APPLE)
    message(FATAL_ERROR "Apple/macOS platform is not supported by this project.")
endif()

# =================================================================================================
# Dependencies — all resolved locally, no engine variables required
# =================================================================================================
include(FetchContent)

# ---- GLFW ---------------------------------------------------------------------------------------
if(NOT TARGET glfw)
    find_package(glfw3 CONFIG QUIET)
endif()


if(NOT TARGET glfw)
    message(STATUS "[launcher] glfw3 not found on system, fetching via FetchContent")
    set(GLFW_BUILD_EXAMPLES                                             OFF CACHE BOOL "" FORCE)
    set(GLFW_BUILD_TESTS                                                OFF CACHE BOOL "" FORCE)
    set(GLFW_BUILD_DOCS                                                 OFF CACHE BOOL "" FORCE)
    set(GLFW_INSTALL                                                    OFF CACHE BOOL "" FORCE)
    FetchContent_Declare(glfw
        GIT_REPOSITORY                                                  https://github.com/glfw/glfw.git
        GIT_TAG                                                         3.4
        GIT_SHALLOW                                                     TRUE
    )
    FetchContent_MakeAvailable(glfw)
endif()

# ---- ImGui --------------------------------------------------------------------------------------
# Reuse the engine's vendored ImGui if it already exists — avoids a duplicate checkout and keeps
# the two builds visually in sync. If not, fetch it into the launcher's own build tree.
set(ENGINE_IMGUI_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../vendor/imgui")


if(EXISTS "${ENGINE_IMGUI_DIR}/imgui.cpp")
    message(STATUS                                                      "[launcher] Reusing engine's vendored ImGui: ${ENGINE_IMGUI_DIR}")
    set(IMGUI_DIR                                                       "${ENGINE_IMGUI_DIR}")
else()
    message(STATUS "[launcher] Engine vendor/imgui not found, fetching via FetchContent")
    FetchContent_Declare(imgui
        GIT_REPOSITORY                                                  https://github.com/ocornut/imgui.git
        GIT_TAG                                                         docking
        GIT_SHALLOW                                                     TRUE
    )
    FetchContent_MakeAvailable(imgui)        # no CMakeLists in imgui → just populates
    set(IMGUI_DIR "${imgui_SOURCE_DIR}")
endif()


find_package(OpenGL REQUIRED)


add_library(glm INTERFACE)
target_include_directories(glm INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}/../vendor/glm
)

# =================================================================================================
# Target
# =================================================================================================

set(GLUTTONY_CORE_SRC                                                   "${CMAKE_CURRENT_SOURCE_DIR}/../core/src")

# Compile all of core except its own entry point. The launcher is the executable;
# core's main/entry_point would produce a duplicate `main`.
file(GLOB_RECURSE SHARED_UTIL_SOURCES CONFIGURE_DEPENDS
    "${GLUTTONY_CORE_SRC}/*.cpp"
    "${GLUTTONY_CORE_SRC}/*.cxx"
    "${GLUTTONY_CORE_SRC}/*.c"
)

# Core's own entry point would collide with launcher's main()
list(FILTER SHARED_UTIL_SOURCES EXCLUDE REGEX "/entry_point\\.cpp$")
list(FILTER SHARED_UTIL_SOURCES EXCLUDE REGEX "/main\\.cpp$")



file(GLOB_RECURSE LAUNCHER_SOURCES                                      CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cxx"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.c"
)


set(IMGUI_LAUNCHER_SOURCES
    ${IMGUI_DIR}/imgui.cpp
    ${IMGUI_DIR}/imgui_draw.cpp
    ${IMGUI_DIR}/imgui_tables.cpp
    ${IMGUI_DIR}/imgui_widgets.cpp
    ${IMGUI_DIR}/imgui_demo.cpp
    ${IMGUI_DIR}/backends/imgui_impl_glfw.cpp
    ${IMGUI_DIR}/backends/imgui_impl_opengl3.cpp
)


add_executable(${PROJECT_NAME}
    ${LAUNCHER_SOURCES}
    ${IMGUI_LAUNCHER_SOURCES}
    ${SHARED_UTIL_SOURCES}
)


set_target_properties(${PROJECT_NAME} PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY                                            "${COMPILE_OUTPUT_DIR}"
    CXX_VISIBILITY_PRESET                                               default
    VISIBILITY_INLINES_HIDDEN                                           OFF
)
if(GCC16_RUNTIME_DIR)
    set_target_properties(${PROJECT_NAME} PROPERTIES
        BUILD_RPATH                                                     "${GCC16_RUNTIME_DIR}"
        INSTALL_RPATH                                                   "${GCC16_RUNTIME_DIR}"
    )
endif()


target_include_directories(${PROJECT_NAME}                              PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src/
    ${GLUTTONY_CORE_SRC}                        # so `#include "util/pch.h"` works
    ${CMAKE_SOURCE_DIR}/../vendor/glm
    ${IMGUI_DIR}
    ${IMGUI_DIR}/backends
)


target_link_libraries(${PROJECT_NAME}                                   PRIVATE
    glfw
    OpenGL::GL
    pthread
    dl
)


target_compile_definitions(${PROJECT_NAME}                              PRIVATE
    $<$<CONFIG:Debug>:DEBUG>
    $<$<CONFIG:Release>:RELEASE>
    $<$<CONFIG:RelWithDebInfo>:RELEASE_WITH_DEBUG_INFO>
    IMGUI_DEFINE_MATH_OPERATORS
    USE_IMGUI
    LINKAGE_STYLE=LINKAGE_STYLE_STATIC
)


target_compile_options(${PROJECT_NAME}                                  PRIVATE
    $<$<COMPILE_LANGUAGE:CXX>:-include${GLUTTONY_CORE_SRC}/util/pch.h>
    $<$<COMPILE_LANGUAGE:CXX>:-std=c++26>
    $<$<COMPILE_LANGUAGE:CXX>:-freflection>
    $<$<COMPILE_LANGUAGE:CXX>:-ftemplate-depth=512>
    $<$<AND:$<COMPILE_LANGUAGE:CXX>,$<CONFIG:Debug>>:-g3>
    $<$<COMPILE_LANGUAGE:CXX>:-fdiagnostics-color=always>
)



# ------------- Copy assets directory to output ---------------------------------------------------
if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/asset)
    add_custom_target(${PROJECT_NAME}_copy_assets ALL
        COMMAND                                                         ${CMAKE_COMMAND} -E copy_directory
                ${CMAKE_CURRENT_SOURCE_DIR}/asset
                ${COMPILE_OUTPUT_DIR}/asset
        COMMENT "Copying assets from component [${PROJECT_NAME}] to build directory: ${COMPILE_OUTPUT_DIR}/asset"
        VERBATIM
    )
    # Make sure assets are only copied after the executable is linked
    add_dependencies(${PROJECT_NAME}_copy_assets ${PROJECT_NAME})
else()
    message(WARNING "Assets directory not found at ${CMAKE_SOURCE_DIR}/asset")
endif()







# vendor ------------------------------------------------------------------------------------
set(VENDOR_DIR                                                          "${CMAKE_SOURCE_DIR}/../vendor")
set(VK_STB_IMAGE_DIR                                                    "${VENDOR_DIR}/stb_image")

# STB ---------------------------------------------------------------------------------------------
target_include_directories(${PROJECT_NAME}                              PRIVATE "${VK_STB_IMAGE_DIR}/include")









# =================================================================================================
# Statically-linked plugins
# =================================================================================================
# The launcher bakes its plugin set into its own executable. No plugins/ directory is scanned at
# runtime — LINKAGE_STYLE=STATIC on the launcher target makes plugin_manager.cpp read only from
# the static plugin registry populated by EXPORT_PLUGIN_CLASS at program startup.
#
# To add a plugin to the launcher:
#   1. Append its directory name (under plugin/) to LAUNCHER_STATIC_PLUGINS below.
#   2. That's it. It gets compiled with its own GLT_MODULE_NAME and linked into the launcher.
#
# Plugins not in this list are never compiled into the launcher and can never be loaded at
# runtime — the launcher simply doesn't know they exist. This is how the feature set is "locked"
# for an exported game.

set(LAUNCHER_STATIC_PLUGINS
    vfs_vfspp
    logger_default
    window_glfw
)


function(add_launcher_static_plugin PLUGIN_NAME PLUGIN_DIR)
    set(target "${PLUGIN_NAME}_launcher_static")

    file(GLOB_RECURSE plugin_sources
        CONFIGURE_DEPENDS
        "${PLUGIN_DIR}/src/*.cpp"
        "${PLUGIN_DIR}/src/*.cxx"
        "${PLUGIN_DIR}/src/*.c"
    )
    if(NOT plugin_sources)
        message(FATAL_ERROR "add_launcher_static_plugin: no sources found under ${PLUGIN_DIR}/src/")
    endif()

    add_library(${target} OBJECT ${plugin_sources})

    target_compile_definitions(${target} PRIVATE
        GLT_MODULE_NAME="${PLUGIN_NAME}"
        LINKAGE_STYLE=LINKAGE_STYLE_STATIC
        $<$<CONFIG:Debug>:DEBUG>
        $<$<CONFIG:Release>:RELEASE>
        $<$<CONFIG:RelWithDebInfo>:RELEASE_WITH_DEBUG_INFO>
    )

    target_include_directories(${target} PRIVATE
        ${GLUTTONY_CORE_SRC}
        ${PLUGIN_DIR}/src/
        ${PLUGIN_DIR}/include/
        ${CMAKE_CURRENT_SOURCE_DIR}/../vendor/glm
        ${IMGUI_DIR}
        ${IMGUI_DIR}/backends
    )

    target_compile_options(${target} PRIVATE
        $<$<COMPILE_LANGUAGE:CXX>:-include${GLUTTONY_CORE_SRC}/util/pch.h>
        $<$<COMPILE_LANGUAGE:CXX>:-std=c++26>
        $<$<COMPILE_LANGUAGE:CXX>:-freflection>
        $<$<COMPILE_LANGUAGE:CXX>:-ftemplate-depth=512>
        $<$<COMPILE_LANGUAGE:CXX>:-fdiagnostics-color=always>
        $<$<AND:$<COMPILE_LANGUAGE:CXX>,$<CONFIG:Debug>>:-g3>
        $<$<COMPILE_LANGUAGE:CXX>:-Wall -Wextra -Wpedantic>
    )

    set_target_properties(${target} PROPERTIES
        POSITION_INDEPENDENT_CODE   ON
        CXX_VISIBILITY_PRESET       default
        VISIBILITY_INLINES_HIDDEN   OFF
    )
endfunction()


foreach(plugin_name ${LAUNCHER_STATIC_PLUGINS})

    set(plugin_dir "${CMAKE_CURRENT_SOURCE_DIR}/../plugin/${plugin_name}")
    if(NOT EXISTS "${plugin_dir}/CMakeLists.txt")
        message(FATAL_ERROR "Launcher static plugin '${plugin_name}' not found at ${plugin_dir}")
    endif()
    add_launcher_static_plugin(${plugin_name} "${plugin_dir}")
    target_link_libraries(${PROJECT_NAME} PRIVATE ${plugin_name}_launcher_static)

endforeach()
