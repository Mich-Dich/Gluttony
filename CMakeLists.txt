# =================================================================================================
# Root CMake Configuration
# =================================================================================================
# Project:      plugin test for Gluttony
# Location:     ${PROJECT_SOURCE_DIR}/CMakeLists.txt
# Purpose:      Orchestrates entire build system, sets global policies
#
# Usage:
#   mkdir build && cd build
#   cmake .. [-DOPTION=VALUE ...]
#   cmake --build .
# =================================================================================================


cmake_minimum_required(VERSION                                          3.20)
project(Gluttony VERSION 0.0                                            LANGUAGES CXX)
include(GNUInstallDirs)
list(PREPEND CMAKE_MODULE_PATH                                          "${CMAKE_SOURCE_DIR}/cmake")
include(${CMAKE_SOURCE_DIR}/helper.cmake)
option(CORE_VERBOSE_INFO                                                "Show detailed file list for Core" OFF)
set(CORE_VERBOSE_INFO                                                   TRUE)
set(CMAKE_CXX_STANDARD                                                  26)
set(CMAKE_CXX_STANDARD_REQUIRED                                         ON)
set(CMAKE_CXX_EXTENSIONS                                                OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS                                       ON)

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
# Vendor Dependencies
# =================================================================================================
# Purpose:      Integrate third-party libraries (submodules, packages, downloads)
# Important:    Defined before core/plugin so their targets are visible to them
# =================================================================================================

set(VENDOR_DIR                                                          "${CMAKE_SOURCE_DIR}/vendor")
set(IMGUI_DIR                                                           "${VENDOR_DIR}/imgui")
set(GLM_DIR                                                             "${VENDOR_DIR}/glm")

# clone libs for core (or ones that are shared across multiple plugins)
git_clone_or_update("git@github.com:icaven/glm.git"                     "${GLM_DIR}" DEPTH 1)
git_clone_or_update("https://github.com/ocornut/imgui.git"              "${IMGUI_DIR}" DEPTH 1 BRANCH docking)

# ------------- GLM -------------------------------------------------------------------------------
add_library(glm                                                         INTERFACE)
target_include_directories(glm                                          INTERFACE
    ${GLM_DIR}
)

# ------------- ImGui -----------------------------------------------------------------------------
# NOTE: imgui is intentionally *not* part of the exported `GluttonyTargets` package.
# Its sources are PRIVATE (must not leak into INTERFACE_SOURCES), its include dirs are
# BUILD_INTERFACE only, and its lib + headers are installed without an EXPORT.
# Consumers get imgui headers via Gluttony::GluttonyAPI's include dirs, and resolve
# imgui symbols at dlopen time against the host's already-loaded libimgui.so.
add_library(imgui                                                       SHARED)
set_target_properties(imgui                                             PROPERTIES
    POSITION_INDEPENDENT_CODE                                           ON
    CXX_VISIBILITY_PRESET                                               default
)
file(GLOB                                                               IMGUI_BACKEND_SOURCES
    ${IMGUI_DIR}/*.cpp
    ${IMGUI_DIR}/backends/imgui_impl_glfw.cpp
    ${IMGUI_DIR}/backends/imgui_impl_vulkan.cpp
)
target_sources(imgui                                                    PRIVATE
    ${IMGUI_BACKEND_SOURCES}
)
target_include_directories(imgui                                        PUBLIC
    $<BUILD_INTERFACE:${IMGUI_DIR}>
    $<BUILD_INTERFACE:${IMGUI_DIR}/backends>
)
find_package(Vulkan                                                     REQUIRED)
target_link_libraries(imgui
    PRIVATE glfw
    PUBLIC  Vulkan::Vulkan
)
target_compile_definitions(imgui                                        PRIVATE
    IMGUI_IMPL_GLFW_DISABLE_X11
    IMGUI_IMPL_VULKAN_NO_PROTOTYPES
    GLFW_INCLUDE_NONE
)

# ------------- Tracy (profiler client) ----------------------------------------------------------
# One shared TracyClient for the executable and every plugin (a single profiler instance across dlopen'd modules).
# On-demand: nothing is collected or sent until the Tracy profiler app connects, so leaving it ON costs ~nothing.
# OFF: GLT_PROFILE_* macros compile to the in-engine recorder only (no Tracy symbols at all)
option(GLT_ENABLE_TRACY                                                 "Instrument the engine for the Tracy profiler" ON)
set(GLT_TRACY_VERSION                                                   "v0.14.1" CACHE STRING "Tracy git tag (client and profiler app must match)")

if(GLT_ENABLE_TRACY)
    set(TRACY_DIR                                                       "${VENDOR_DIR}/tracy")
    git_clone_or_update("https://github.com/wolfpld/tracy.git"          "${TRACY_DIR}" DEPTH 1 TAG ${GLT_TRACY_VERSION})

    add_library(tracy_client                                            SHARED
        ${TRACY_DIR}/public/TracyClient.cpp
    )
    set_target_properties(tracy_client                                  PROPERTIES
        POSITION_INDEPENDENT_CODE                                       ON
        CXX_VISIBILITY_PRESET                                           default
        LIBRARY_OUTPUT_DIRECTORY                                        "${CMAKE_BINARY_DIR}/bin"
    )
    target_include_directories(tracy_client                             PUBLIC
        $<BUILD_INTERFACE:${TRACY_DIR}/public>
    )
    target_compile_definitions(tracy_client                             PUBLIC
        TRACY_ENABLE
        TRACY_ON_DEMAND
        GLT_TRACY_ENABLED
        GLT_TRACY_SOURCE_DIR="${TRACY_DIR}"
    )
    target_link_libraries(tracy_client                                  PUBLIC ${LINUX_SYSTEM_LIBS})
    install(TARGETS tracy_client
        LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    )
    message(STATUS "Tracy ${GLT_TRACY_VERSION} enabled (on-demand). Profiler app: build ${TRACY_DIR}/profiler or install the same version")
endif()

# =================================================================================================
# Build resource limits (RAM-friendly build)
# =================================================================================================
# Ninja job pools cap concurrent link jobs independently of compile jobs
# Linking is by far the biggest RAM spike (Vulkan + ImGui + SoLoud + Slang)
# Compile jobs are already capped by CMAKE_BUILD_PARALLEL_LEVEL in the build preset
set_property(GLOBAL PROPERTY JOB_POOLS                                  link_pool=24)


# Core ================================================================================================================
add_subdirectory(core)

# Package install =====================================================================================================
# imgui lib (no EXPORT — just places the .so on disk for optional standalone testing)
install(TARGETS imgui
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
)
# imgui headers -> <prefix>/include/imgui.h, <prefix>/include/backends/*.h
install(DIRECTORY ${IMGUI_DIR}/
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
    FILES_MATCHING PATTERN "*.h"
)
# glm headers -> <prefix>/include/glm/*.hpp
install(DIRECTORY ${GLM_DIR}/glm/
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/glm
    FILES_MATCHING PATTERN "*.hpp" PATTERN "*.h" PATTERN "*.inl"
)

include(CMakePackageConfigHelpers)
configure_package_config_file(
    "${CMAKE_SOURCE_DIR}/cmake/gluttony_config.cmake.in"
    "${CMAKE_BINARY_DIR}/gluttony_config.cmake"
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/Gluttony
)
write_basic_package_version_file(
    "${CMAKE_BINARY_DIR}/GluttonyConfigVersion.cmake"
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY SameMajorVersion
)
install(FILES
    "${CMAKE_BINARY_DIR}/gluttony_config.cmake"
    "${CMAKE_BINARY_DIR}/GluttonyConfigVersion.cmake"
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/Gluttony
)
install(EXPORT GluttonyTargets
    FILE GluttonyTargets.cmake
    NAMESPACE Gluttony::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/Gluttony
)

# Plugins - intentionally last, their installs may fail ===============================================================
add_subdirectory(plugin)
