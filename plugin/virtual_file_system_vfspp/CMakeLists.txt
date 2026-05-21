# =============================================================================
# Specific Plugin CMake Configuration
# =============================================================================
# Project:      plugin test for Gluttony
# Location:     ${PROJECT_SOURCE_DIR}/plugin/PLUGIN_NAME/CMakeLists.txt
# Purpose:      compile the plugin in this die as a shared lib
# =============================================================================

# =============================================================================
# Specific Plugin CMake Configuration
# =============================================================================
include(${CMAKE_SOURCE_DIR}/helper.cmake)
set(PLUGIN_NAME                                         "virtual_file_system_vfspp")

file(GLOB_RECURSE PLUGIN_SOURCES
    CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cxx"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.c"
)
set_property(GLOBAL PROPERTY "${CMAKE_CURRENT_SOURCE_DIR}_SOURCES" "${PLUGIN_SOURCES}")

# --- vfspp integration -------------------------------------------------------
set(VFSPP_DIR "${CMAKE_CURRENT_SOURCE_DIR}/vendor/vfspp")

if(NOT EXISTS "${VFSPP_DIR}")
    message(STATUS "Cloning vfspp into ${VFSPP_DIR} ...")
    execute_process(
        COMMAND git clone --recurse-submodules https://github.com/nextgeniuspro/vfspp.git "${VFSPP_DIR}"
        RESULT_VARIABLE clone_result
        ERROR_VARIABLE clone_error
    )
    if(NOT clone_result EQUAL 0)
        message(FATAL_ERROR "Failed to clone vfspp: ${clone_error}")
    endif()

    # Patch miniz-cpp: fix std::uint64_t → mz_uint64 in write/read callbacks
    set(MINIZ_HPP "${VFSPP_DIR}/vendor/miniz-cpp/zip_file.hpp")
    if(EXISTS "${MINIZ_HPP}")
        execute_process(
            # Change 'std::uint64_t file_ofs' to 'mz_uint64 file_ofs'
            # in lines that contain 'write_callback' or 'read_callback'
            COMMAND sed -i "/write_callback\\|read_callback/ s/std::uint64_t file_ofs/mz_uint64 file_ofs/g" "${MINIZ_HPP}"
            RESULT_VARIABLE patch_result
        )
        if(NOT patch_result EQUAL 0)
            message(WARNING "Failed to patch miniz-cpp. Compilation might fail on LP64 systems.")
        else()
            message(STATUS "Patched miniz-cpp for LP64 compatibility.")
        endif()
    endif()
endif()

add_subdirectory("${VFSPP_DIR}" "${CMAKE_BINARY_DIR}/vfspp_build")
target_include_directories(vfspp SYSTEM INTERFACE "$<BUILD_INTERFACE:${VFSPP_DIR}/include>")    # Mark as SYSTEM to suppress warnings

# compile the plugin
compile_plugin(SHARED ${PLUGIN_NAME})
compile_plugin(STATIC ${PLUGIN_NAME}_static)

target_link_libraries(${PLUGIN_NAME}        PUBLIC vfspp)
target_compile_features(${PLUGIN_NAME}      PUBLIC cxx_std_20)
target_compile_definitions(${PLUGIN_NAME}   PUBLIC VFSPP_MT_SUPPORT_ENABLED)
# No need for explicit target_include_directories – vfspp INTERFACE handles it

target_link_libraries(${PLUGIN_NAME}_static        PUBLIC vfspp)
target_compile_features(${PLUGIN_NAME}_static      PUBLIC cxx_std_20)
target_compile_definitions(${PLUGIN_NAME}_static   PUBLIC VFSPP_MT_SUPPORT_ENABLED)



print_source_file_details(
    SOURCES         ${PLUGIN_SOURCES}
    NAME            ${PLUGIN_NAME}
    SOURCE_DIR      "${CMAKE_CURRENT_SOURCE_DIR}"
    VERBOSE         CORE_VERBOSE_INFO
    TARGET          ${PLUGIN_NAME}
)
