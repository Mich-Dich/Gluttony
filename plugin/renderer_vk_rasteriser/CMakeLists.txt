# ===========================================================================================
# Specific Plugin CMake Configuration
# ===========================================================================================
# Project:      renderer plugin for Gluttony
# Location:     ${PROJECT_SOURCE_DIR}/plugin/PLUGIN_NAME/CMakeLists.txt
# Purpose:      compile the plugin in this dir as a Vulkan renderer
# ===========================================================================================

include(${CMAKE_SOURCE_DIR}/helper.cmake)
set(PLUGIN_NAME                                                         "renderer_vk_rasterizer")

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
target_compile_definitions(${PLUGIN_NAME}                               PRIVATE USE_IMGUI)

find_package(glslang REQUIRED)
target_link_libraries(${PLUGIN_NAME}                                    PRIVATE glslang)
target_link_libraries(${PLUGIN_NAME}_static                             PRIVATE glslang)

# --- ensures thw dependencies are present in vendor/ ---------------------------------------
set(VENDOR_DIR                                                          "${CMAKE_SOURCE_DIR}/vendor")
set(VK_STB_IMAGE_DIR                                                    "${VENDOR_DIR}/stb_image")
set(VULKAN_HEADERS_DIR                                                  "${VENDOR_DIR}/Vulkan-Headers")
set(VK_BOOTSTRAP_DIR                                                    "${VENDOR_DIR}/vk-bootstrap")
set(VMA_DIR                                                             "${VENDOR_DIR}/VulkanMemoryAllocator")
file(MAKE_DIRECTORY                                                     "${VENDOR_DIR}")

git_clone_or_update("git@github.com:franko/stb_image.git"                                       "${VK_STB_IMAGE_DIR}")
git_clone_or_update("https://github.com/KhronosGroup/Vulkan-Headers.git"                        "${VULKAN_HEADERS_DIR}" BRANCH "vulkan-sdk-1.4.357")
git_clone_or_update("git@github.com:charles-lunarg/vk-bootstrap.git"                            "${VK_BOOTSTRAP_DIR}" TAG "v1.4.328")
git_clone_or_update("https://github.com/GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator.git"     "${VMA_DIR}")

target_include_directories(${PLUGIN_NAME}                               PRIVATE "${VK_STB_IMAGE_DIR}/include")
target_include_directories(${PLUGIN_NAME}_static                        PRIVATE "${VK_STB_IMAGE_DIR}/include")

set(VK_BOOTSTRAP_VULKAN_HEADERS_DIR "${VULKAN_HEADERS_DIR}/include"     CACHE PATH "Path to Vulkan headers" FORCE)

if(NOT TARGET vk-bootstrap)
    add_subdirectory("${VK_BOOTSTRAP_DIR}"                              "${CMAKE_BINARY_DIR}/vendor/vk-bootstrap")
endif()
set_target_properties(vk-bootstrap                                      PROPERTIES POSITION_INDEPENDENT_CODE ON) 

target_include_directories(${PLUGIN_NAME}                               SYSTEM PRIVATE "${PROJECT_SOURCE_DIR}/vendor/vk-bootstrap/src/")
target_include_directories(${PLUGIN_NAME}_static                        SYSTEM PRIVATE "${PROJECT_SOURCE_DIR}/vendor/vk-bootstrap/src/")
target_link_libraries(${PLUGIN_NAME}                                    PRIVATE vk-bootstrap)
target_link_libraries(${PLUGIN_NAME}_static                             PRIVATE vk-bootstrap)

if(NOT TARGET VulkanMemoryAllocator)
    add_subdirectory("${VMA_DIR}"                                       "${CMAKE_BINARY_DIR}/vendor/VulkanMemoryAllocator")
endif()

target_include_directories(${PLUGIN_NAME}                               SYSTEM PRIVATE "${VMA_DIR}/include")
target_include_directories(${PLUGIN_NAME}_static                        SYSTEM PRIVATE "${VMA_DIR}/include")
target_link_libraries(${PLUGIN_NAME}                                    PRIVATE VulkanMemoryAllocator)
target_link_libraries(${PLUGIN_NAME}_static                             PRIVATE VulkanMemoryAllocator)

set(VENDOR_DIR                                                          "${CMAKE_SOURCE_DIR}/vendor")
set(IMGUI_DIR                                                           "${VENDOR_DIR}/imgui")
target_include_directories(${PLUGIN_NAME}                               PRIVATE "${IMGUI_DIR}")
target_include_directories(${PLUGIN_NAME}_static                        PRIVATE "${IMGUI_DIR}")



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
