# ===========================================================================================
# Plugin Dir CMake Configuration
# ===========================================================================================
# Project:      plugin test for Gluttony
# Location:     ${PROJECT_SOURCE_DIR}/plugin/CMakeLists.txt
# Purpose:      compile all plugins that are in this dir
# ===========================================================================================

set(PLUGIN_OUTPUT_DIR "${CMAKE_BINARY_DIR}/plugins-$<CONFIG>")

# Helper function (optional but recommended) so plugins share common settings
function(add_plugin PLUGIN_NAME)
    add_library(${PLUGIN_NAME}                      MODULE ${ARGN})
    set_target_properties(${PLUGIN_NAME}            PROPERTIES
        LIBRARY_OUTPUT_DIRECTORY "${PLUGIN_OUTPUT_DIR}"
        PREFIX ""   # prevents "lib" prefix on Linux/macOS
    )
    target_compile_features(${PLUGIN_NAME}          PRIVATE cxx_std_17)
endfunction()

# Scan for directories directly under this folder
file(GLOB PLUGIN_DIRS
    LIST_DIRECTORIES true
    CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/*"
)

foreach(dir ${PLUGIN_DIRS})
    if(IS_DIRECTORY "${dir}" AND EXISTS "${dir}/CMakeLists.txt")
        # Get the relative directory name (optional, for display)
        get_filename_component(plugin_name "${dir}" NAME)
        add_subdirectory("${dir}")
    endif()
endforeach()
