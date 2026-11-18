# ===========================================================================================
# Specific Plugin CMake Configuration
# ===========================================================================================
# Project:      plugin for Gluttony - spaceship construction (Spaceship editor, spaceship pack)
# Location:     ${PROJECT_SOURCE_DIR}/plugin/construct_spaceship/CMakeLists.txt
# Purpose:      interface::custom, enable it in the plugin UI. Uses the procedural construction system of the core
#               (core/src/procedural/construct, see documentation/procedural_construction.md)
# ===========================================================================================

include(${CMAKE_SOURCE_DIR}/helper.cmake)
set(PLUGIN_NAME                                                         "construct_spaceship")

file(GLOB_RECURSE PLUGIN_SOURCES
    CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp"
)
set_property(GLOBAL PROPERTY "${CMAKE_CURRENT_SOURCE_DIR}_SOURCES"      "${PLUGIN_SOURCES}")

compile_plugin(SHARED                                                   ${PLUGIN_NAME}          glm imgui)
compile_plugin(STATIC                                                   ${PLUGIN_NAME}_static   glm imgui)
target_compile_definitions(${PLUGIN_NAME}                               PRIVATE USE_IMGUI IMGUI_DEFINE_MATH_OPERATORS)
target_compile_definitions(${PLUGIN_NAME}_static                        PRIVATE USE_IMGUI IMGUI_DEFINE_MATH_OPERATORS)

print_source_file_details(
    SOURCES                                                             ${PLUGIN_SOURCES}
    NAME                                                                ${PLUGIN_NAME}
    SOURCE_DIR                                                          "${CMAKE_CURRENT_SOURCE_DIR}"
    VERBOSE                                                             CORE_VERBOSE_INFO
    TARGET                                                              ${PLUGIN_NAME}
)
