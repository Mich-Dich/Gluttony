# ===========================================================================================
# Specific Plugin CMake Configuration
# ===========================================================================================
# Project:      plugin test for Gluttony
# Location:     ${PROJECT_SOURCE_DIR}/plugin/PLUGIN_NAME/CMakeLists.txt
# Purpose:      compile the plugin in this die as a shared lib
# ===========================================================================================

include(${CMAKE_SOURCE_DIR}/helper.cmake)
set(PLUGIN_NAME                                                         "asset_factory_mesh_assimp")

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

# vendor ------------------------------------------------------------------------------------
set(VENDOR_DIR                                                          "${CMAKE_CURRENT_SOURCE_DIR}/vendor")
set(ASSIMP_DIR                                                          "${VENDOR_DIR}/assimp")
git_clone_or_update("git@github.com:assimp/assimp.git"                  "${ASSIMP_DIR}")

# ASSIMP ------------------------------------------------------------------------------------------
set(ASSIMP_BUILD_TESTS                                                  OFF CACHE BOOL "" FORCE)
set(ASSIMP_BUILD_ASSIMP_TOOLS                                           OFF CACHE BOOL "" FORCE)
set(ASSIMP_BUILD_SAMPLES                                                OFF CACHE BOOL "" FORCE)
set(ASSIMP_BUILD_ZLIB                                                   ON  CACHE BOOL "" FORCE)
set(ASSIMP_BUILD_ZLIB                                                   ON  CACHE BOOL "" FORCE)   # ensure ON even if a parent forced OFF
unset(ZLIB_LIBRARY                                                      CACHE)
unset(ZLIB_LIBRARIES                                                    CACHE)
unset(ZLIB_INCLUDE_DIR                                                  CACHE)
unset(ZLIB_INCLUDE_DIRS                                                 CACHE)
set(ASSIMP_BUILD_ALL_IMPORTERS_BY_DEFAULT                               OFF CACHE BOOL "" FORCE)
set(ASSIMP_BUILD_OBJ_IMPORTER                                           ON CACHE BOOL "" FORCE)
set(ASSIMP_BUILD_FBX_IMPORTER                                           ON CACHE BOOL "" FORCE)

add_subdirectory("${ASSIMP_DIR}"                                        "${CMAKE_BINARY_DIR}/assimp" EXCLUDE_FROM_ALL)

target_link_libraries(${PLUGIN_NAME}                                    PRIVATE assimp)
target_link_libraries(${PLUGIN_NAME}_static                             PRIVATE assimp)



print_source_file_details(
    SOURCES                                                             ${PLUGIN_SOURCES}
    NAME                                                                ${PLUGIN_NAME}
    SOURCE_DIR                                                          "${CMAKE_CURRENT_SOURCE_DIR}"
    VERBOSE                                                             CORE_VERBOSE_INFO
    TARGET                                                              ${PLUGIN_NAME}
)
