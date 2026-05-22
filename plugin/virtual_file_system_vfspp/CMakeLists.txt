# ===========================================================================================
# Specific Plugin CMake Configuration
# ===========================================================================================
# Project:      plugin test for Gluttony
# Location:     ${PROJECT_SOURCE_DIR}/plugin/PLUGIN_NAME/CMakeLists.txt
# Purpose:      compile the plugin in this dir as a shared lib
# ===========================================================================================

include(${CMAKE_SOURCE_DIR}/helper.cmake)
set(PLUGIN_NAME                                                     "virtual_file_system_vfspp")

# Gather plugin sources
file(GLOB_RECURSE PLUGIN_SOURCES
    CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cxx"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.c"
)
set_property(GLOBAL PROPERTY "${CMAKE_CURRENT_SOURCE_DIR}_SOURCES"  "${PLUGIN_SOURCES}")

# --- Ensure vfspp is cloned, patched, and built --------------------------------------------
function(ensure_vfspp                                               VENDOR_DIR)
    file(MAKE_DIRECTORY                                             "${VENDOR_DIR}")
    set(VFSPP_DIR                                                   "${VENDOR_DIR}/vfspp")

    git_clone_or_update("https://github.com/nextgeniuspro/vfspp.git" "${VFSPP_DIR}" DEPTH 1)    # Clone/fetch using the generic helper

    execute_process(                                                # Initialise submodules (vfspp needs miniz-cpp etc.)
        COMMAND git submodule update --init --recursive
        WORKING_DIRECTORY                                           "${VFSPP_DIR}"
        RESULT_VARIABLE                                             submodule_result
    )
    if(NOT submodule_result EQUAL 0)
        message(FATAL_ERROR "Failed to update submodules for vfspp")
    endif()

    set(MINIZ_HPP "${VFSPP_DIR}/vendor/miniz-cpp/zip_file.hpp")     # Patch miniz-cpp for LP64 compatibility
    if(EXISTS "${MINIZ_HPP}")
        execute_process(
            COMMAND sed -i "/write_callback\\|read_callback/ s/std::uint64_t file_ofs/mz_uint64 file_ofs/g" "${MINIZ_HPP}"
            RESULT_VARIABLE patch_result
        )
        if(NOT patch_result EQUAL 0)
            message(WARNING "Failed to patch miniz-cpp. Compilation might fail on LP64 systems.")
        else()
            message(STATUS "Patched miniz-cpp for LP64 compatibility.")
        endif()
    endif()

    add_subdirectory("${VFSPP_DIR}"                                 "${CMAKE_BINARY_DIR}/vfspp_build")          # Add the vfspp subproject
    target_include_directories(vfspp SYSTEM INTERFACE               "$<BUILD_INTERFACE:${VFSPP_DIR}/include>")  # Suppress warnings from vfspp headers
endfunction()

ensure_vfspp("${CMAKE_CURRENT_SOURCE_DIR}/vendor")

# --- Compile the plugin as both shared and static ------------------------------------------
compile_plugin(SHARED                                               "${PLUGIN_NAME}")
compile_plugin(STATIC                                               "${PLUGIN_NAME}_static")

# Link vfspp and set compilation features/definitions
target_link_libraries("${PLUGIN_NAME}"                              PUBLIC vfspp)
target_link_libraries("${PLUGIN_NAME}_static"                       PUBLIC vfspp)

target_compile_features("${PLUGIN_NAME}"                            PUBLIC cxx_std_20)
target_compile_features("${PLUGIN_NAME}_static"                     PUBLIC cxx_std_20)

target_compile_definitions("${PLUGIN_NAME}"                         PUBLIC VFSPP_MT_SUPPORT_ENABLED)
target_compile_definitions("${PLUGIN_NAME}_static"                  PUBLIC VFSPP_MT_SUPPORT_ENABLED)


# --- Print source file statistics (verbose controlled by CORE_VERBOSE_INFO) ----------------
print_source_file_details(
    SOURCES                                                         ${PLUGIN_SOURCES}
    NAME                                                            "${PLUGIN_NAME}"
    SOURCE_DIR                                                      "${CMAKE_CURRENT_SOURCE_DIR}"
    VERBOSE                                                         CORE_VERBOSE_INFO
    TARGET                                                          "${PLUGIN_NAME}"
)
