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
set(SLANG_DIR                                                           "${VENDOR_DIR}/slang")
file(MAKE_DIRECTORY                                                     "${VENDOR_DIR}")

git_clone_or_update("git@github.com:franko/stb_image.git"                                       "${VK_STB_IMAGE_DIR}")
git_clone_or_update("https://github.com/KhronosGroup/Vulkan-Headers.git"                        "${VULKAN_HEADERS_DIR}" BRANCH "vulkan-sdk-1.4.357")
git_clone_or_update("git@github.com:charles-lunarg/vk-bootstrap.git"                            "${VK_BOOTSTRAP_DIR}" TAG "v1.4.328")
git_clone_or_update("https://github.com/GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator.git"     "${VMA_DIR}")
git_clone_or_update("https://github.com/shader-slang/slang.git"                                 "${SLANG_DIR}" TAG "v2025.1")



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






# ---------------------------------------------------------------------------------------------
# Slang SDK — version 2025.1
# ---------------------------------------------------------------------------------------------

set(SLANG_VERSION      "2025.1"       CACHE STRING "Slang prebuilt release tag (without the leading v)")
set(SLANG_ARCHIVE_NAME "slang-${SLANG_VERSION}-linux-x86_64")
set(SLANG_PREBUILT_ROOT "${VENDOR_DIR}/slang-prebuilt")
set(SLANG_PREFIX_DIR   "${SLANG_PREBUILT_ROOT}/slang-${SLANG_VERSION}")
set(SLANG_TARBALL      "${SLANG_PREBUILT_ROOT}/${SLANG_ARCHIVE_NAME}.tar.gz")

# The archive carries a top-level `slang-<version>-linux-x86_64/` directory, so we extract
# into a staging area and then locate the real `lib/` directory inside it. This is also
# tolerant of the 2025.11+ rename from libslang.so to libslang-compiler.so.
if(NOT EXISTS "${SLANG_PREFIX_DIR}/lib/libslang.so" AND
   NOT EXISTS "${SLANG_PREFIX_DIR}/lib/libslang-compiler.so")

    message(STATUS "Slang ${SLANG_VERSION} not present at ${SLANG_PREFIX_DIR}, fetching ...")
    file(MAKE_DIRECTORY "${SLANG_PREFIX_DIR}")

    if(NOT EXISTS "${SLANG_TARBALL}")
        message(STATUS "Downloading ${SLANG_ARCHIVE_NAME}.tar.gz ...")
        file(DOWNLOAD
            "https://github.com/shader-slang/slang/releases/download/v${SLANG_VERSION}/${SLANG_ARCHIVE_NAME}.tar.gz"
            "${SLANG_TARBALL}"
            SHOW_PROGRESS
            STATUS dl_status
        )
        list(GET dl_status 0 dl_code)
        if(NOT dl_code EQUAL 0)
            list(GET dl_status 1 dl_msg)
            message(FATAL_ERROR
                "Slang download failed (${dl_msg}).\n"
                "URL: https://github.com/shader-slang/slang/releases/download/v${SLANG_VERSION}/${SLANG_ARCHIVE_NAME}.tar.gz\n"
                "Check the tag/asset name at https://github.com/shader-slang/slang/releases")
        endif()
    endif()

    # 1. Extract (VERBOSE shows what CMake is actually unpacking).
    file(ARCHIVE_EXTRACT
        INPUT       "${SLANG_TARBALL}"
        DESTINATION "${SLANG_PREFIX_DIR}"
        VERBOSE
    )

    # 2. The archive has a wrapper directory; find the real `lib/` inside it.
    file(GLOB_RECURSE _slang_lib_candidates
        "${SLANG_PREFIX_DIR}/lib/libslang.so"
        "${SLANG_PREFIX_DIR}/lib/libslang-compiler.so"
    )
    list(LENGTH _slang_lib_candidates _slang_lib_count)
    if(_slang_lib_count EQUAL 0)
        file(GLOB _slang_extracted "${SLANG_PREFIX_DIR}/*")
        message(FATAL_ERROR
            "Extracted Slang archive, but no libslang.so / libslang-compiler.so was found\n"
            "under ${SLANG_PREFIX_DIR}.\n"
            "Contents of ${SLANG_PREFIX_DIR}:\n  ${_slang_extracted}\n"
            "Inspect the tarball with `tar -tzf ${SLANG_TARBALL}` to confirm its layout.")
    endif()
    list(GET _slang_lib_candidates 0 _slang_lib_path)
    get_filename_component(_slang_lib_dir  "${_slang_lib_path}" DIRECTORY)   # .../lib
    get_filename_component(SLANG_PREFIX_DIR "${_slang_lib_dir}"  DIRECTORY)  # .../
    message(STATUS "Slang library dir: ${_slang_lib_dir}")
endif()

message(STATUS "Using Slang prebuilt at: ${SLANG_PREFIX_DIR}")

# ---------------------------------------------------------------------------------------------
# Imported target
# ---------------------------------------------------------------------------------------------
if(NOT TARGET slang)
    add_library(slang SHARED IMPORTED)
    # Pick whichever name the release actually shipped.
    if(EXISTS "${SLANG_PREFIX_DIR}/lib/libslang.so")
        set(_slang_soname "libslang.so")
    else()
        set(_slang_soname "libslang-compiler.so")
    endif()
    set_target_properties(slang PROPERTIES
        IMPORTED_LOCATION              "${SLANG_PREFIX_DIR}/lib/${_slang_soname}"
        INTERFACE_INCLUDE_DIRECTORIES  "${SLANG_PREFIX_DIR}/include"
    )
endif()

target_link_libraries(${PLUGIN_NAME}          PRIVATE slang)
target_link_libraries(${PLUGIN_NAME}_static   PRIVATE slang)

foreach(_slang_target IN ITEMS ${PLUGIN_NAME} ${PLUGIN_NAME}_static)
    set_target_properties(${_slang_target} PROPERTIES
        BUILD_RPATH   "${SLANG_PREFIX_DIR}/lib"
        INSTALL_RPATH "${SLANG_PREFIX_DIR}/lib"
        BUILD_WITH_INSTALL_RPATH TRUE
    )
endforeach()







# ------------- Copy assets directory to output ---------------------------------------------------
set(COMPILE_OUTPUT_DIR                                                  "${CMAKE_BINARY_DIR}/bin")
if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/asset)
    add_custom_target(${PLUGIN_NAME}_copy_assets                          ALL
        COMMAND                                                         ${CMAKE_COMMAND} -E copy_directory
            ${CMAKE_CURRENT_SOURCE_DIR}/asset
            ${COMPILE_OUTPUT_DIR}/asset
        COMMENT "Copying assets from component [${PLUGIN_NAME}] to build directory: ${COMPILE_OUTPUT_DIR}/asset"
        VERBATIM
    )
    # Make sure assets are only copied after the executable is linked
    add_dependencies(${PLUGIN_NAME}_copy_assets ${PLUGIN_NAME})
    add_dependencies(${PLUGIN_NAME}_copy_assets ${PLUGIN_NAME}_static)
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
