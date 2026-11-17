# ===========================================================================================
# Specific Plugin CMake Configuration
# ===========================================================================================
# Project:      plugin for Gluttony - terrain generator
# Location:     ${PROJECT_SOURCE_DIR}/plugin/terrain/CMakeLists.txt
# Purpose:      node graph based terrain generation (interface::custom, enable it in the plugin UI)
#               see documentation/terrain_generation.md
# ===========================================================================================

include(${CMAKE_SOURCE_DIR}/helper.cmake)
set(PLUGIN_NAME                                                         "terrain")

file(GLOB_RECURSE PLUGIN_SOURCES
    CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cxx"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.c"
)
set_property(GLOBAL PROPERTY "${CMAKE_CURRENT_SOURCE_DIR}_SOURCES"      "${PLUGIN_SOURCES}")

# vendor ------------------------------------------------------------------------------------
set(VENDOR_DIR                                                          "${CMAKE_SOURCE_DIR}/vendor")
set(IMGUI_DIR                                                           "${VENDOR_DIR}/imgui")
set(VNS_DIR                                                             "${VENDOR_DIR}/visual_node_system")
# Our fork of VisualNodeSystem (node frame / socket layout hooks, socket hit area incl. labels, settings + zoom limit
# access, font). It is developed in place: LIVE = full clone on a branch, later only fast-forwarded, never while it has
# uncommitted changes or unpushed commits (see git_clone_or_update in helper.cmake)
git_clone_or_update("https://github.com/Mich-Dich/visual_node_system.git" "${VNS_DIR}" LIVE BRANCH master)

# VisualNodeSystem --------------------------------------------------------------------------
# Built here instead of through its own CMakeLists: that one sets global flags / include dirs and wants to compile its own
# ImGui. We compile the same sources against the engine's ImGui (shared lib, one context for the editor and the canvas)
if(NOT TARGET glt_visual_node_system)

    set(VNS_SOURCES
        "${VNS_DIR}/VisualNodeSystem.cpp"
        "${VNS_DIR}/GroupComment.cpp"
        "${VNS_DIR}/VisualNodeFactory.cpp"
        "${VNS_DIR}/VisualNode.cpp"
        "${VNS_DIR}/VisualNodeSocket.cpp"
        "${VNS_DIR}/VisualNodeCore.cpp"
        "${VNS_DIR}/SubSystems/VisualNodeArea/VisualNodeArea.cpp"
        "${VNS_DIR}/SubSystems/VisualNodeArea/VisualNodeAreaLogic.cpp"
        "${VNS_DIR}/SubSystems/VisualNodeArea/VisualNodeAreaInput.cpp"
        "${VNS_DIR}/SubSystems/VisualNodeArea/VisualNodeAreaRendering.cpp"
        "${VNS_DIR}/SubSystems/TextInputPopup.cpp"
        "${VNS_DIR}/StandardNodes/BoundaryNodes/SocketMirrorNode.cpp"
        "${VNS_DIR}/StandardNodes/BoundaryNodes/LinkNode/LinkNode.cpp"
        "${VNS_DIR}/StandardNodes/BoundaryNodes/SubAreaNode/SubAreaNode.cpp"
        "${VNS_DIR}/StandardNodes/BoundaryNodes/SubAreaNode/SubAreaInputNode.cpp"
        "${VNS_DIR}/StandardNodes/BoundaryNodes/SubAreaNode/SubAreaOutputNode.cpp"
        "${VNS_DIR}/ThirdParty/jsoncpp/json_reader.cpp"
        "${VNS_DIR}/ThirdParty/jsoncpp/json_value.cpp"
        "${VNS_DIR}/ThirdParty/jsoncpp/json_writer.cpp"
        # VNS includes misc/cpp/imgui_stdlib.h; the engine's imgui lib does not compile the .cpp
        "${IMGUI_DIR}/misc/cpp/imgui_stdlib.cpp"
    )

    add_library(glt_visual_node_system                                  STATIC ${VNS_SOURCES})
    set_target_properties(glt_visual_node_system                        PROPERTIES
        POSITION_INDEPENDENT_CODE                                       ON
        CXX_STANDARD                                                    20
        CXX_STANDARD_REQUIRED                                           ON
        ARCHIVE_OUTPUT_DIRECTORY                                        "${CMAKE_BINARY_DIR}/bin-int/vendor/visual_node_system"
    )
    target_compile_definitions(glt_visual_node_system                   PUBLIC  IMGUI_DEFINE_MATH_OPERATORS)
    # SYSTEM: the plugin builds with -Wall -Wextra -Wpedantic, the vendor headers should not flood that output
    target_include_directories(glt_visual_node_system                   SYSTEM PUBLIC
        "${VNS_DIR}"
        "${VNS_DIR}/ThirdParty"
        "${VNS_DIR}/ThirdParty/jsoncpp"
        "${VNS_DIR}/ThirdParty/stduuid"
        "${IMGUI_DIR}"
    )
    target_link_libraries(glt_visual_node_system                        PUBLIC  glm imgui)
    if(MSVC)
        target_compile_options(glt_visual_node_system                   PRIVATE /w)
    else()
        target_compile_options(glt_visual_node_system                   PRIVATE -w)
    endif()

    if(UNIX AND NOT APPLE)
        find_package(X11 REQUIRED)
        target_link_libraries(glt_visual_node_system                    PUBLIC  X11::X11)
    endif()

    # VNS includes a generated version header (VersionInfo/), create it at configure time like its own CMakeLists does
    execute_process(COMMAND ${CMAKE_COMMAND}
        -D PROJECT_VERSION_PREFIX=VISUAL_NODE_SYSTEM_
        -D PROJECT_VERSION_MAJOR=0
        -D PROJECT_VERSION_MINOR=3
        -D PROJECT_VERSION_PATCH=0
        -D PROJECT_VERSION_DIR=${VNS_DIR}/VersionInfo
        -P ${VNS_DIR}/VersionInfo/UpdateProjectVersion.cmake
    )
endif()

# add libraries in both static and shared
compile_plugin(SHARED                                                   ${PLUGIN_NAME}          glm imgui glt_visual_node_system)
compile_plugin(STATIC                                                   ${PLUGIN_NAME}_static   glm imgui glt_visual_node_system)
target_compile_definitions(${PLUGIN_NAME}                               PRIVATE USE_IMGUI)
target_compile_definitions(${PLUGIN_NAME}_static                        PRIVATE USE_IMGUI)

# Seamless chunks need bit-identical arithmetic per sample, whatever region it is evaluated in: no fused multiply-add
# contraction (the compiler could fuse in one code path and not in another)
if(NOT MSVC)
    target_compile_options(${PLUGIN_NAME}                               PRIVATE -ffp-contract=off)
    target_compile_options(${PLUGIN_NAME}_static                        PRIVATE -ffp-contract=off)
endif()



print_source_file_details(
    SOURCES                                                             ${PLUGIN_SOURCES}
    NAME                                                                ${PLUGIN_NAME}
    SOURCE_DIR                                                          "${CMAKE_CURRENT_SOURCE_DIR}"
    VERBOSE                                                             CORE_VERBOSE_INFO
    TARGET                                                              ${PLUGIN_NAME}
)
