# =================================================================================================
# GCC 16.2 toolchain — auto-detects the compiler and exports GCC16_RUNTIME_DIR for RPATH use.
# =================================================================================================
# Detection order:
#   1. $GCC16_ROOT/bin/{gcc-16.2, gcc-16, gcc}
#   2. Known install prefixes: /opt/gcc-16.2, /opt/gcc-16, /usr/local/gcc-16.2, ...
#   3. PATH lookup for gcc-16.2 / gcc-16 / gcc
# Fails hard if the resolved compiler is not GCC 16.x.
# =================================================================================================

if(_GCC16_TOOLCHAIN_DONE)
    return()
endif()
set(_GCC16_TOOLCHAIN_DONE TRUE)

set(_gcc16_candidate_roots
    "$ENV{GCC16_ROOT}"
    "/opt/gcc-16.2"
    "/opt/gcc-16"
    "/opt/gcc16"
    "/usr/local/gcc-16.2"
    "/usr/local/gcc-16"
)

set(_gcc16_cc  "")
set(_gcc16_cxx "")

# 1) Preferred install prefixes
foreach(_root IN LISTS _gcc16_candidate_roots)
    if(NOT _root)
        continue()
    endif()
    foreach(_cc_name gcc-16.2 gcc-16 gcc)
        if(_gcc16_cc)
            break()
        endif()
        if(EXISTS "${_root}/bin/${_cc_name}")
            string(REPLACE "gcc" "g++" _cxx_name "${_cc_name}")
            if(EXISTS "${_root}/bin/${_cxx_name}")
                set(_gcc16_cc  "${_root}/bin/${_cc_name}")
                set(_gcc16_cxx "${_root}/bin/${_cxx_name}")
            endif()
        endif()
    endforeach()
endforeach()

# 2) Fall back to PATH
if(NOT _gcc16_cc)
    find_program(_gcc16_cc  NAMES gcc-16.2 gcc-16 gcc)
    find_program(_gcc16_cxx NAMES g++-16.2 g++-16 g++)
endif()

if(NOT _gcc16_cc OR NOT _gcc16_cxx)
    message(FATAL_ERROR
        "GCC 16.2 not found. Set GCC16_ROOT to its install prefix "
        "(e.g. -DGCC16_ROOT=/opt/gcc-16.2 or export GCC16_ROOT=/opt/gcc-16.2), "
        "or place gcc-16.2/g++-16.2 on PATH.")
endif()

# 3) Verify the detected compiler really is 16.x
execute_process(
    COMMAND "${_gcc16_cc}" -dumpversion
    OUTPUT_VARIABLE _gcc16_version
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET
)
if(NOT _gcc16_version MATCHES "^16")
    message(FATAL_ERROR
        "Auto-detected ${_gcc16_cc} reports version '${_gcc16_version}', "
        "but the project requires GCC 16.x. Set GCC16_ROOT to override.")
endif()

# 4) Commit the compiler — plain set(); CMake caches it during project()
set(CMAKE_C_COMPILER   "${_gcc16_cc}")
set(CMAKE_CXX_COMPILER "${_gcc16_cxx}")

message(STATUS "[gcc-16.2] Using GCC ${_gcc16_version}")
message(STATUS "[gcc-16.2]   CC  = ${_gcc16_cc}")
message(STATUS "[gcc-16.2]   CXX = ${_gcc16_cxx}")

# 5) Derive the runtime lib dir for RPATH (used by core & launcher CMakeLists)
get_filename_component(_gcc16_bin_dir "${_gcc16_cc}"     DIRECTORY)
get_filename_component(_gcc16_prefix  "${_gcc16_bin_dir}" DIRECTORY)

if(EXISTS "${_gcc16_prefix}/lib64/libstdc++.so")
    set(GCC16_RUNTIME_DIR "${_gcc16_prefix}/lib64" CACHE INTERNAL "GCC 16.2 runtime libs")
elseif(EXISTS "${_gcc16_prefix}/lib/libstdc++.so")
    set(GCC16_RUNTIME_DIR "${_gcc16_prefix}/lib"   CACHE INTERNAL "GCC 16.2 runtime libs")
else()
    set(GCC16_RUNTIME_DIR "${_gcc16_prefix}/lib64" CACHE INTERNAL "GCC 16.2 runtime libs (fallback)")
endif()
message(STATUS "[gcc-16.2]   runtime libs = ${GCC16_RUNTIME_DIR}")
