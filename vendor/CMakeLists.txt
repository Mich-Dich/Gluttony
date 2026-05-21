# =============================================================================
# Vendor Dependencies
# =============================================================================
# Location:     ${PROJECT_SOURCE_DIR}/vendor/CMakeLists.txt
# Purpose:      Integrate third-party libraries (submodules, packages, downloads)
# Important:    Builds before core to satisfy dependencies
# =============================================================================


include(${CMAKE_SOURCE_DIR}/helper.cmake)


set(VENDOR_DIR                                                  "${CMAKE_SOURCE_DIR}/vendor")
set(IMGUI_DIR                                                   "${VENDOR_DIR}/imgui")
set(GLM_DIR                                                     "${VENDOR_DIR}/glm")

# clone libs for core (or ones that are shared accross multiple plugins)
git_clone_or_update("git@github.com:icaven/glm.git"             "${GLM_DIR}" DEPTH 1)
git_clone_or_update("https://github.com/ocornut/imgui.git"      "${IMGUI_DIR}" DEPTH 1 BRANCH docking)



# ------------- GLM --------------------------------------------------------------------------------
add_library(glm                                                 INTERFACE)
target_include_directories(glm                                  INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}/glm
)

# ------------- ImGui ------------------------------------------------------------------------------
add_library(imgui                                               STATIC)
set_target_properties(imgui PROPERTIES                          POSITION_INDEPENDENT_CODE ON)
file(GLOB IMGUI_BACKEND_SOURCES                                 # Add ImGui backends (GLFW + Vulkan)
    ${CMAKE_CURRENT_SOURCE_DIR}/imgui/*.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/imgui/backends/imgui_impl_glfw.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/imgui/backends/imgui_impl_vulkan.cpp
)
target_sources(imgui PRIVATE                                    # Add all sources to the imgui library
    ${IMGUI_BACKEND_SOURCES}
)
target_include_directories(imgui                                PUBLIC  # Include directories
    ${CMAKE_CURRENT_SOURCE_DIR}/imgui
    ${CMAKE_CURRENT_SOURCE_DIR}/imgui/backends
)
target_link_libraries(imgui                                     PUBLIC  # Link dependencies
    glfw
    # Vulkan::Vulkan
)
target_compile_definitions(imgui                                PRIVATE  # Add compile definitions for imgui
    IMGUI_IMPL_GLFW_DISABLE_X11
    IMGUI_IMPL_VULKAN_NO_PROTOTYPES                             # Use Vulkan SDK headers instead of volk
    GLFW_INCLUDE_NONE                                           # Don't include OpenGL headers
)
