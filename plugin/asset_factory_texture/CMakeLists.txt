# ===========================================================================================
# Specific Plugin CMake Configuration
# ===========================================================================================
# Project:      plugin test for Gluttony
# Location:     ${PROJECT_SOURCE_DIR}/plugin/PLUGIN_NAME/CMakeLists.txt
# Purpose:      compile the plugin in this die as a shared lib
# ===========================================================================================

include(${CMAKE_SOURCE_DIR}/helper.cmake)
set(PLUGIN_NAME                                                         "asset_factory_texture")

file(GLOB_RECURSE PLUGIN_SOURCES
    CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cxx"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.c"
)
set_property(GLOBAL PROPERTY "${CMAKE_CURRENT_SOURCE_DIR}_SOURCES"      "${PLUGIN_SOURCES}")

# add libraries in both static and shared
compile_plugin(SHARED                                                   ${PLUGIN_NAME}          glm)
compile_plugin(STATIC                                                   ${PLUGIN_NAME}_static   glm)

# vendor ------------------------------------------------------------------------------------
set(VENDOR_DIR                                                          "${CMAKE_CURRENT_SOURCE_DIR}/vendor")
set(VK_STB_IMAGE_DIR                                                    "${VENDOR_DIR}/stb_image")
git_clone_or_update("git@github.com:franko/stb_image.git"               "${VK_STB_IMAGE_DIR}")

# STB ---------------------------------------------------------------------------------------------
target_include_directories(${PLUGIN_NAME}                               PRIVATE "${VK_STB_IMAGE_DIR}/include")
target_include_directories(${PLUGIN_NAME}_static                        PRIVATE "${VK_STB_IMAGE_DIR}/include")



print_source_file_details(
    SOURCES                                                             ${PLUGIN_SOURCES}
    NAME                                                                ${PLUGIN_NAME}
    SOURCE_DIR                                                          "${CMAKE_CURRENT_SOURCE_DIR}"
    VERBOSE                                                             CORE_VERBOSE_INFO
    TARGET                                                              ${PLUGIN_NAME}
)
