# # ===========================================================================================
# # Specific Plugin CMake Configuration
# # ===========================================================================================
# # Project:      plugin test for Gluttony
# # Location:     ${PROJECT_SOURCE_DIR}/plugin/PLUGIN_NAME/CMakeLists.txt
# # Purpose:      compile the plugin in this die as a shared lib
# # ===========================================================================================

# include(${CMAKE_SOURCE_DIR}/helper.cmake)
# set(PLUGIN_NAME                                                         "physics_jolt")

# file(GLOB_RECURSE PLUGIN_SOURCES
#     CONFIGURE_DEPENDS
#     "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp"
#     "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cxx"
#     "${CMAKE_CURRENT_SOURCE_DIR}/src/*.c"
# )
# set_property(GLOBAL PROPERTY "${CMAKE_CURRENT_SOURCE_DIR}_SOURCES"      "${PLUGIN_SOURCES}")

# # add libraries in both static and shared
# compile_plugin(SHARED                                                   ${PLUGIN_NAME}          glm imgui)
# compile_plugin(STATIC                                                   ${PLUGIN_NAME}_static   glm imgui)





# # ---------------------------------------------------------------------------
# # Jolt Physics — fetch + build as a subproject
# # ---------------------------------------------------------------------------
# include(FetchContent)

# set(VENDOR_DIR                                                          "${CMAKE_SOURCE_DIR}/vendor")
# set(JOLT_PHYSICS_DIR                                                    "${VENDOR_DIR}/JoltPhysics")

# # Disable everything we don't need — MUST be set BEFORE FetchContent_MakeAvailable
# set(TARGET_UNIT_TESTS                                                   OFF CACHE BOOL "" FORCE)
# set(TARGET_HELLO_WORLD                                                  OFF CACHE BOOL "" FORCE)
# set(TARGET_SAMPLES                                                      OFF CACHE BOOL "" FORCE)
# set(TARGET_VIEWER                                                       OFF CACHE BOOL "" FORCE)
# set(TARGET_PERFORMANCE_TEST                                             OFF CACHE BOOL "" FORCE)

# # Performance toggles
# set(INTERPROCEDURAL_OPTIMIZATION                                        ON CACHE BOOL "" FORCE)
# set(USE_AVX2                                                            ON CACHE BOOL "" FORCE)

# FetchContent_Declare(
#     JoltPhysics
#     GIT_REPOSITORY  https://github.com/jrouwe/JoltPhysics.git
#     GIT_TAG         v5.2.0                    # pin a tag, never use main
#     GIT_SHALLOW     TRUE
#     SOURCE_DIR      "${JOLT_PHYSICS_DIR}"     # clone INTO your vendor dir
# )

# FetchContent_MakeAvailable(JoltPhysics)

# # ---------------------------------------------------------------------------
# # Link Jolt into the plugin targets
# # ---------------------------------------------------------------------------
# target_link_libraries(${PLUGIN_NAME}                                    PRIVATE Jolt)
# target_link_libraries(${PLUGIN_NAME}_static                             PRIVATE Jolt)

# target_include_directories(${PLUGIN_NAME}                               PRIVATE ${JOLT_PHYSICS_DIR})
# target_include_directories(${PLUGIN_NAME}_static                        PRIVATE ${JOLT_PHYSICS_DIR})





# print_source_file_details(
#     SOURCES                                                             ${PLUGIN_SOURCES}
#     NAME                                                                ${PLUGIN_NAME}
#     SOURCE_DIR                                                          "${CMAKE_CURRENT_SOURCE_DIR}"
#     VERBOSE                                                             CORE_VERBOSE_INFO
#     TARGET                                                              ${PLUGIN_NAME}
# )
