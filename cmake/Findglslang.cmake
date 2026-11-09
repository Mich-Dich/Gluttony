# =================================================================================================
# Findglslang.cmake
# =================================================================================================
# Provides `glslang::*` targets to projects that call `find_package(glslang)`.
#
# Resolution order:
#   1. System config package (distro package or Vulkan SDK: glslangConfig.cmake)
#   2. FetchContent from KhronosGroup/glslang, built with minimal options
#
# Override the fallback tag with -DGLSLANG_FETCH_TAG=...
# =================================================================================================

if(TARGET glslang::glslang)
    set(glslang_FOUND                   TRUE)
    return()
endif()

# 1) System config package — CONFIG mode prevents recursion into this module
find_package(glslang                    CONFIG QUIET)
if(glslang_FOUND)
    message(STATUS "[Findglslang] using system glslang (${glslang_DIR})")
    return()
endif()

# 2) FetchContent fallback
message(STATUS "[Findglslang] system glslang not found, fetching via FetchContent")

include(FetchContent)

# Minimal build: no optimizer, no HLSL, no CLI tools, no install rules.
# ENABLE_OPT=OFF skips SPIRV-Tools entirely — much faster to build.
set(ENABLE_OPT                          OFF CACHE BOOL "" FORCE)
set(ENABLE_HLSL                         OFF CACHE BOOL "" FORCE)
set(ENABLE_GLSLANG_BINARIES             OFF CACHE BOOL "" FORCE)
set(BUILD_EXTERNAL                      OFF CACHE BOOL "" FORCE)
set(BUILD_TESTING                       OFF CACHE BOOL "" FORCE)
set(GLSLANG_TESTS                       OFF CACHE BOOL "" FORCE)
set(GLSLANG_ENABLE_INSTALL              OFF CACHE BOOL "" FORCE)

if(NOT DEFINED                          GLSLANG_FETCH_TAG)
    set(GLSLANG_FETCH_TAG               "15.1.0")
endif()

FetchContent_Declare(glslang
    GIT_REPOSITORY                      https://github.com/KhronosGroup/glslang.git
    GIT_TAG                             ${GLSLANG_FETCH_TAG}
    GIT_SHALLOW                         TRUE
)
FetchContent_MakeAvailable(glslang)

if(NOT TARGET glslang::glslang)
    message(FATAL_ERROR
        "[Findglslang] FetchContent succeeded but glslang::glslang target is missing. "
        "The fetched glslang version (${GLSLANG_FETCH_TAG}) may be too old to expose "
        "namespaced ALIAS targets — pin a newer tag with -DGLSLANG_FETCH_TAG=...")
endif()

set(glslang_FOUND                       TRUE)
message(STATUS "[Findglslang] using fetched glslang ${GLSLANG_FETCH_TAG}")
