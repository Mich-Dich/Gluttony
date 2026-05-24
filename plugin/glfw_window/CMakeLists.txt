# ===========================================================================================
# Specific Plugin CMake Configuration
# ===========================================================================================
# Project:      plugin test for Gluttony
# Location:     ${PROJECT_SOURCE_DIR}/plugin/PLUGIN_NAME/CMakeLists.txt
# Purpose:      compile the plugin in this die as a shared lib
# ===========================================================================================

include(${CMAKE_SOURCE_DIR}/helper.cmake)
set(PLUGIN_NAME                                                     "glfw_window")

file(GLOB_RECURSE PLUGIN_SOURCES
    CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cxx"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.c"
)
set_property(GLOBAL PROPERTY "${CMAKE_CURRENT_SOURCE_DIR}_SOURCES"  "${PLUGIN_SOURCES}")

# add libraries in both static and shared
compile_plugin(SHARED                                               ${PLUGIN_NAME}          glm imgui)
compile_plugin(STATIC                                               ${PLUGIN_NAME}_static   glm imgui)


# --- ensures GLFW is present in vendor/ and links it ---------------------------------------
function(ensure_glfw                                                VENDOR_DIR)
    file(MAKE_DIRECTORY                                             "${VENDOR_DIR}")
    set(GLFW_DIR                                                    "${VENDOR_DIR}/glfw")
    git_clone_or_update("https://github.com/glfw/glfw.git" "${GLFW_DIR}" DEPTH 1)           # Use the generic helper (depth 1 is default)

    set(GLFW_BUILD_OPENGL      OFF CACHE BOOL "Disable OpenGL support"             FORCE)
    set(GLFW_BUILD_WAYLAND     OFF CACHE BOOL "Disable Wayland support"            FORCE)
    set(GLFW_BUILD_EXAMPLES    OFF CACHE BOOL "Build the GLFW example programs"    FORCE)
    set(GLFW_BUILD_TESTS       OFF CACHE BOOL "Build the GLFW test programs"       FORCE)
    set(GLFW_BUILD_DOCS        OFF CACHE BOOL "Build the GLFW documentation"       FORCE)
    set(GLFW_INSTALL           OFF CACHE BOOL "Generate installation target"       FORCE)

    target_compile_definitions(${PLUGIN_NAME}                       PRIVATE GLFW_INCLUDE_NONE)
    add_subdirectory("${GLFW_DIR}")
endfunction()

ensure_glfw("${CMAKE_CURRENT_SOURCE_DIR}/vendor")
target_link_libraries(${PLUGIN_NAME}                                PRIVATE glfw)


set(VENDOR_DIR                                                      "${CMAKE_SOURCE_DIR}/vendor")
set(IMGUI_DIR                                                       "${VENDOR_DIR}/imgui")
target_include_directories(${PLUGIN_NAME}                           PRIVATE "${IMGUI_DIR}")
target_include_directories(${PLUGIN_NAME}_static                    PRIVATE "${IMGUI_DIR}")



print_source_file_details(
    SOURCES                                                         ${PLUGIN_SOURCES}
    NAME                                                            ${PLUGIN_NAME}
    SOURCE_DIR                                                      "${CMAKE_CURRENT_SOURCE_DIR}"
    VERBOSE                                                         CORE_VERBOSE_INFO
    TARGET                                                          ${PLUGIN_NAME}
)
