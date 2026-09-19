# ===========================================================================================
# Specific Plugin CMake Configuration
# ===========================================================================================
# Project:      plugin test for Gluttony
# Location:     ${PROJECT_SOURCE_DIR}/plugin/PLUGIN_NAME/CMakeLists.txt
# Purpose:      compile the plugin in this die as a shared lib
# ===========================================================================================

include(${CMAKE_SOURCE_DIR}/helper.cmake)
set(PLUGIN_NAME                                                         "editor")

file(GLOB_RECURSE PLUGIN_SOURCES
    CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cxx"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.c"
)
set_property(GLOBAL PROPERTY "${CMAKE_CURRENT_SOURCE_DIR}_SOURCES" "${PLUGIN_SOURCES}")

# add libraries in both static and shared
compile_plugin(SHARED                                                   ${PLUGIN_NAME}          glm imgui)
compile_plugin(STATIC                                                   ${PLUGIN_NAME}_static   glm imgui)
target_compile_definitions(${PLUGIN_NAME}                               PRIVATE
    IMGUI_DEFINE_MATH_OPERATORS
    USE_IMGUI
)

# vendor ------------------------------------------------------------------------------------
set(VENDOR_DIR                                                          "${CMAKE_CURRENT_SOURCE_DIR}/vendor")
set(IMPLOT_DIR                                                          "${VENDOR_DIR}/implot")
set(IMGUI_DIR                                                           "${VENDOR_DIR}/imgui")
set(STB_IMAGE_DIR                                                       "${VENDOR_DIR}/stb_image")
set(DR_LIBS_DIR                                                         "${VENDOR_DIR}/dr_libs")
set(IMGUI_COLOR_TEXT_EDIT_DIR                                           "${VENDOR_DIR}/ImGuiColorTextEdit")
git_clone_or_update("git@github.com:epezent/implot.git"                 "${IMPLOT_DIR}")
git_clone_or_update("git@github.com:franko/stb_image.git"               "${STB_IMAGE_DIR}")
git_clone_or_update("https://github.com/mackron/dr_libs.git"            "${DR_LIBS_DIR}")
git_clone_or_update("https://github.com/goossens/ImGuiColorTextEdit"    "${IMGUI_COLOR_TEXT_EDIT_DIR}")

# ImPlot ------------------------------------------------------------------------------------------
set(IMPLOT_SOURCES
    "${IMPLOT_DIR}/implot.cpp"
    "${IMPLOT_DIR}/implot_items.cpp"
    # "${IMPLOT_DIR}/implot_demo.cpp"   # optional, only if you want the demo window
)
target_include_directories(${PLUGIN_NAME}                               PRIVATE "${IMPLOT_DIR}")
target_include_directories(${PLUGIN_NAME}_static                        PRIVATE "${IMPLOT_DIR}")
target_sources(${PLUGIN_NAME}                                           PRIVATE ${IMPLOT_SOURCES})
target_sources(${PLUGIN_NAME}_static                                    PRIVATE ${IMPLOT_SOURCES})

# ImGui -------------------------------------------------------------------------------------------
target_include_directories(${PLUGIN_NAME}                               PRIVATE "${IMGUI_DIR}")
target_include_directories(${PLUGIN_NAME}_static                        PRIVATE "${IMGUI_DIR}")

# ImGuiColorTextEdit ------------------------------------------------------------------------------
target_include_directories(${PLUGIN_NAME}                               PRIVATE "${IMGUI_COLOR_TEXT_EDIT_DIR}")
target_include_directories(${PLUGIN_NAME}_static                        PRIVATE "${IMGUI_COLOR_TEXT_EDIT_DIR}")
set(IMGUI_COLOR_TEXT_EDIT_SOURCES
    "${IMGUI_COLOR_TEXT_EDIT_DIR}/TextEditor.cpp"
    # "${IMGUI_COLOR_TEXT_EDIT_DIR}/TextDiff.cpp"                         # Uncomment for diff view
)
target_sources(${PLUGIN_NAME}                                           PRIVATE ${IMGUI_COLOR_TEXT_EDIT_SOURCES})
target_sources(${PLUGIN_NAME}_static                                    PRIVATE ${IMGUI_COLOR_TEXT_EDIT_SOURCES})

# STB image ---------------------------------------------------------------------------------------
target_include_directories(${PLUGIN_NAME}                               PRIVATE "${STB_IMAGE_DIR}/include")
target_include_directories(${PLUGIN_NAME}_static                        PRIVATE "${STB_IMAGE_DIR}/include")

# dr_libs------------------------------------------------------------------------------------------
target_include_directories(${PLUGIN_NAME}                               PRIVATE "${DR_LIBS_DIR}")
target_include_directories(${PLUGIN_NAME}_static                        PRIVATE "${DR_LIBS_DIR}")

# ------------- Copy assets directory to output ---------------------------------------------------
set(COMPILE_OUTPUT_DIR                                                  "${CMAKE_BINARY_DIR}/bin/$<CONFIG>")
if(EXISTS ${CMAKE_SOURCE_DIR}/asset)
    add_custom_target(${PLUGIN_NAME}_copy_assets                          ALL
        COMMAND                                                         ${CMAKE_COMMAND} -E copy_directory
            ${CMAKE_CURRENT_SOURCE_DIR}/asset
            ${COMPILE_OUTPUT_DIR}/asset
        COMMENT "Copying assets to build directory: ${COMPILE_OUTPUT_DIR}/asset"
        VERBATIM
    )
    # Make sure assets are only copied after the executable is linked
    add_dependencies(${PLUGIN_NAME}_copy_assets ${PLUGIN_NAME})
else()
    message(WARNING "Assets directory not found at ${CMAKE_SOURCE_DIR}/asset")
endif()



print_source_file_details(
    SOURCES                                                             ${PLUGIN_SOURCES}
    NAME                                                                ${PLUGIN_NAME}
    SOURCE_DIR                                                          "${CMAKE_CURRENT_SOURCE_DIR}"
    VERBOSE                                                             CORE_VERBOSE_INFO
    TARGET                                                              ${PLUGIN_NAME}
)
