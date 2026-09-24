# ===========================================================================================
# Specific Plugin CMake Configuration
# ===========================================================================================
# Project:      plugin for Gluttony - entt-backed world
# Location:     ${PROJECT_SOURCE_DIR}/plugin/world_ecs_entt/CMakeLists.txt
# Purpose:      default i_world_plugin implementation
# ===========================================================================================

include(${CMAKE_SOURCE_DIR}/helper.cmake)
set(PLUGIN_NAME                                                         "world_ecs_entt")

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

# clone all vendor libs ---------------------------------------------------------------------
set(VENDOR_DIR                                                          "${CMAKE_CURRENT_SOURCE_DIR}/vendor")
set(ENTT_DIR                                                            "${VENDOR_DIR}/entt")
git_clone_or_update("git@github.com:skypjack/entt.git"                  "${ENTT_DIR}")

# OWN PUBLIC HEADERS ------------------------------------------------------------------------
# consumers target_include_directories the [include/] dir to use like this: <world_ecs_entt/...>.
target_include_directories(${PLUGIN_NAME}                               PUBLIC  "${CMAKE_CURRENT_SOURCE_DIR}/include/world_ecs_entt")
target_include_directories(${PLUGIN_NAME}_static                        PUBLIC  "${CMAKE_CURRENT_SOURCE_DIR}/include/world_ecs_entt")

# ENTT --------------------------------------------------------------------------------------
# entt's include dir. PUBLIC so consumers of world_ecs_entt also get it.
target_include_directories(${PLUGIN_NAME}                               PUBLIC  "${ENTT_DIR}/src")
target_include_directories(${PLUGIN_NAME}_static                        PUBLIC  "${ENTT_DIR}/src")



print_source_file_details(
    SOURCES                                                             ${PLUGIN_SOURCES}
    NAME                                                                ${PLUGIN_NAME}
    SOURCE_DIR                                                          "${CMAKE_CURRENT_SOURCE_DIR}"
    VERBOSE                                                             CORE_VERBOSE_INFO
    TARGET                                                              ${PLUGIN_NAME}
)
