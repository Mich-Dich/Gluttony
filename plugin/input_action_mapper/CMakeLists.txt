# =============================================================================
# Specific Plugin CMake Configuration
# =============================================================================
# Project:      plugin test for Gluttony
# Location:     ${PROJECT_SOURCE_DIR}/plugin/PLUGIN_NAME/CMakeLists.txt
# Purpose:      compile the plugin in this die as a shared lib
# =============================================================================

include(${CMAKE_SOURCE_DIR}/helper.cmake)
set(PLUGIN_NAME                                     "input_action_mapper")

file(GLOB_RECURSE PLUGIN_SOURCES
    CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cxx"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.c"
)
set_property(GLOBAL PROPERTY "${CMAKE_CURRENT_SOURCE_DIR}_SOURCES" "${PLUGIN_SOURCES}")

# add libraries in both static and shared
compile_plugin(SHARED                               ${PLUGIN_NAME}          glm)
compile_plugin(STATIC                               ${PLUGIN_NAME}_static   glm)


print_source_file_details(
    SOURCES                                         ${PLUGIN_SOURCES}
    NAME                                            ${PLUGIN_NAME}
    SOURCE_DIR                                      "${CMAKE_CURRENT_SOURCE_DIR}"
    VERBOSE                                         CORE_VERBOSE_INFO
    TARGET                                          ${PLUGIN_NAME}
)
