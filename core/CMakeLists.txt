# ===========================================================================================
# Root CMake Configuration
# ===========================================================================================
# Project:      plugin test for Gluttony
# Location:     ${PROJECT_SOURCE_DIR}/core/CMakeLists.txt
# Purpose:      compile the core of the application (microkernel)
# ===========================================================================================

SET(CORE_NAME                                           "Gluttony")

file(GLOB_RECURSE                                       CORE_SOURCES
    CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cxx"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.c"
)
add_executable(${CORE_NAME}                             ${CORE_SOURCES})
target_compile_definitions(${CORE_NAME}                 PRIVATE GLT_MODULE_NAME="${CORE_NAME}")
target_include_directories(${CORE_NAME}                 PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src/
    ${CMAKE_SOURCE_DIR}/vendor/glm
)

set(COMPILE_OUTPUT_DIR                                  "${CMAKE_BINARY_DIR}/bin/$<CONFIG>")
set_target_properties(${CORE_NAME} PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY                            "${COMPILE_OUTPUT_DIR}"
)
# For the main executable target (e.g., "Gluttony")
target_link_options(${CORE_NAME}                        PRIVATE -rdynamic)
target_link_libraries(${CORE_NAME}                      PRIVATE
    glm
    imgui
)

# Find PCH header if it exists
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/src/util/pch.h")
    # Enable precompiled headers globally for this target
    target_precompile_headers(${CORE_NAME}              PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/src/util/pch.h"
    )

    # Force all files to use the PCH
    target_compile_options(${CORE_NAME}                 PRIVATE
        "$<$<CXX_COMPILER_ID:MSVC>:/FI${CMAKE_CURRENT_SOURCE_DIR}/src/util/pch.h>"
        "$<$<CXX_COMPILER_ID:GNU>:-include${CMAKE_CURRENT_SOURCE_DIR}/src/util/pch.h>"
        "$<$<CXX_COMPILER_ID:Clang>:-include${CMAKE_CURRENT_SOURCE_DIR}/src/util/pch.h>"
    )
endif()



# Copy assets directory to output
if(EXISTS ${CMAKE_SOURCE_DIR}/assets)
    # For single-config generators (make, ninja)
    add_custom_command(TARGET                           ${CORE_NAME}
        POST_BUILD COMMAND                              ${CMAKE_COMMAND} -E copy_directory
            ${CMAKE_SOURCE_DIR}/assets
            ${COMPILE_OUTPUT_DIR}/assets
        COMMENT "Copying assets to build directory: ${COMPILE_OUTPUT_DIR}/assets"
    )
else()
    message(WARNING "Assets directory not found at ${CMAKE_SOURCE_DIR}/assets")
endif()



include(${CMAKE_SOURCE_DIR}/helper.cmake)
print_source_file_details(
    SOURCES                                             ${CORE_SOURCES}
    NAME                                                ${CORE_NAME}
    SOURCE_DIR                                          "${CMAKE_CURRENT_SOURCE_DIR}"
    VERBOSE                                             CORE_VERBOSE_INFO
    TARGET                                              ${CORE_NAME}
)
