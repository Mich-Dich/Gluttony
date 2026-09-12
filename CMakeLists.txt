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

include(${CMAKE_SOURCE_DIR}/helper.cmake)

cmake_minimum_required(VERSION                                          3.20)
project(Gluttony VERSION 0.0                                            LANGUAGES CXX)
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

set(VENDOR_DIR "${CMAKE_SOURCE_DIR}/vendor")
set(IMGUI_DIR  "${VENDOR_DIR}/imgui")
set(GLM_DIR    "${VENDOR_DIR}/glm")

# clone libs for core (or ones that are shared across multiple plugins)
git_clone_or_update("git@github.com:icaven/glm.git"        "${GLM_DIR}"   DEPTH 1)
git_clone_or_update("https://github.com/ocornut/imgui.git" "${IMGUI_DIR}" DEPTH 1 BRANCH docking)

# ------------- GLM -------------------------------------------------------------------------------
add_library(glm INTERFACE)
target_include_directories(glm INTERFACE
    ${GLM_DIR}
)

# ------------- ImGui -----------------------------------------------------------------------------
add_library(imgui SHARED)
set_target_properties(imgui PROPERTIES
    POSITION_INDEPENDENT_CODE ON
    CXX_VISIBILITY_PRESET     default
)
file(GLOB IMGUI_BACKEND_SOURCES
    ${IMGUI_DIR}/*.cpp
    ${IMGUI_DIR}/backends/imgui_impl_glfw.cpp
    ${IMGUI_DIR}/backends/imgui_impl_vulkan.cpp
)
target_sources(imgui PUBLIC
    ${IMGUI_BACKEND_SOURCES}
)
target_include_directories(imgui PUBLIC
    ${IMGUI_DIR}
    ${IMGUI_DIR}/backends
)
find_package(Vulkan REQUIRED)
target_link_libraries(imgui PUBLIC
    glfw
    Vulkan::Vulkan
)
target_compile_definitions(imgui PRIVATE
    IMGUI_IMPL_GLFW_DISABLE_X11
    IMGUI_IMPL_VULKAN_NO_PROTOTYPES
    GLFW_INCLUDE_NONE
)

# ====== Core and plugins =========================================================================
add_subdirectory(core)
add_subdirectory(plugin)
