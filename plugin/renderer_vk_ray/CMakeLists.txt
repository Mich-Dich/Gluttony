# =============================================================================
# Specific Plugin CMake Configuration
# =============================================================================
# Project:      plugin test for Gluttony
# Location:     ${PROJECT_SOURCE_DIR}/plugin/PLUGIN_NAME/CMakeLists.txt
# Purpose:      compile the plugin in this die as a shared lib
# =============================================================================

include(${CMAKE_SOURCE_DIR}/helper.cmake)
set(PLUGIN_NAME                                                     "renderer_vk_ray")

file(GLOB_RECURSE PLUGIN_SOURCES
    CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cxx"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.c"
)
set_property(GLOBAL PROPERTY "${CMAKE_CURRENT_SOURCE_DIR}_SOURCES" "${PLUGIN_SOURCES}")

# add libraries in both static and shared
compile_plugin(SHARED                                               ${PLUGIN_NAME}          glm imgui)
compile_plugin(STATIC                                               ${PLUGIN_NAME}_static   glm imgui)

# --- ensures vk_ray and its dependencies are present in vendor/ ------------
set(VENDOR_DIR                                                      "${CMAKE_CURRENT_SOURCE_DIR}/vendor")
set(VK_BOOTSTRAP_DIR                                                "${VENDOR_DIR}/vk-bootstrap")
set(VK_RAY_DIR                                                      "${VENDOR_DIR}/vk_ray")
set(VMA_DIR                                                         "${VENDOR_DIR}/VulkanMemoryAllocator")
file(MAKE_DIRECTORY                                                 "${VENDOR_DIR}")

git_clone_or_update("git@github.com:charles-lunarg/vk-bootstrap.git"                            "${VK_BOOTSTRAP_DIR}" DEPTH 1)
git_clone_or_update("git@github.com:Mich-Dich/vk_ray.git"                                       "${VK_RAY_DIR}" DEPTH 1)
git_clone_or_update("https://github.com/GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator.git"     "${VMA_DIR}" DEPTH 1)

add_subdirectory("${VK_BOOTSTRAP_DIR}")
set_target_properties(vk-bootstrap                                  PROPERTIES POSITION_INDEPENDENT_CODE ON) 

add_subdirectory("${VMA_DIR}")                                      # header-only, just brings the directory into the build
set(VK_RAY_USE_EXTERNAL_DEPS ON                                     CACHE BOOL "Use external vk-bootstrap and VMA" FORCE)

add_subdirectory("${VK_RAY_DIR}")
set_target_properties(vk_ray                                        PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_include_directories(vk_ray                                   PUBLIC
    "${VMA_DIR}/include"
    "${CMAKE_SOURCE_DIR}/core/src"
)

target_include_directories(${PLUGIN_NAME}                           PRIVATE "${VK_RAY_DIR}/include")
target_include_directories(${PLUGIN_NAME}_static                    PRIVATE "${VK_RAY_DIR}/include")

set(VENDOR_DIR                                                      "${CMAKE_SOURCE_DIR}/vendor")
set(IMGUI_DIR                                                       "${VENDOR_DIR}/imgui")
target_include_directories(${PLUGIN_NAME}                           PRIVATE "${IMGUI_DIR}")
target_include_directories(${PLUGIN_NAME}_static                    PRIVATE "${IMGUI_DIR}")

target_link_libraries(${PLUGIN_NAME}                                PRIVATE vk_ray)
target_link_libraries(${PLUGIN_NAME}_static                         PRIVATE vk_ray)


print_source_file_details(
    SOURCES                                                         ${PLUGIN_SOURCES}
    NAME                                                            ${PLUGIN_NAME}
    SOURCE_DIR                                                      "${CMAKE_CURRENT_SOURCE_DIR}"
    VERBOSE                                                         CORE_VERBOSE_INFO
    TARGET                                                          ${PLUGIN_NAME}
)
