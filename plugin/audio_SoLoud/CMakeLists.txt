# ===========================================================================================
# Specific Plugin CMake Configuration
# ===========================================================================================
# Project:      plugin test for Gluttony
# Location:     ${PROJECT_SOURCE_DIR}/plugin/PLUGIN_NAME/CMakeLists.txt
# Purpose:      compile the plugin in this die as a shared lib
# ===========================================================================================

include(${CMAKE_SOURCE_DIR}/helper.cmake)
set(PLUGIN_NAME                                                         "audio_soloud")

# We compile SoLoud's vendored C sources (stb_vorbis.c, dr_*.c, etc.).
# The parent project enables only CXX, so we must opt in to C here.
enable_language(C)

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


# clone all vendor libs ---------------------------------------------------------------------
set(VENDOR_DIR                                                          "${CMAKE_CURRENT_SOURCE_DIR}/vendor")
set(SOLOUD_DIR                                                          "${VENDOR_DIR}/soloud")
git_clone_or_update("git@github.com:jarikomppa/soloud.git"              "${SOLOUD_DIR}")


# SoLoud core + the sources actually needed
file(GLOB_RECURSE SOLOUD_SOURCES
    "${SOLOUD_DIR}/src/core/*.cpp"
    "${SOLOUD_DIR}/src/filter/*.cpp"
    "${SOLOUD_DIR}/src/audiosource/wav/*.cpp"      # Wav + WavStream (dr_wav, dr_mp3, dr_flac)
    "${SOLOUD_DIR}/src/audiosource/wav/*.c"
    "${SOLOUD_DIR}/src/audiosource/monotone/*.cpp" # optional, tiny synth
    "${SOLOUD_DIR}/src/audiosource/sfxr/*.cpp"     # optional, retro SFX generator
    "${SOLOUD_DIR}/src/audiosource/speech/*.cpp"   # optional, speech synth
    "${SOLOUD_DIR}/src/backend/miniaudio/*.cpp"    # <-- the backend
)

# Silence all warnings from third-party SoLoud + miniaudio sources.
set_source_files_properties(${SOLOUD_SOURCES} PROPERTIES                COMPILE_OPTIONS "-w")

target_sources(${PLUGIN_NAME}                                           PRIVATE ${SOLOUD_SOURCES})
target_sources(${PLUGIN_NAME}_static                                    PRIVATE ${SOLOUD_SOURCES})

target_include_directories(${PLUGIN_NAME}                               PRIVATE "${SOLOUD_DIR}/include")
target_include_directories(${PLUGIN_NAME}_static                        PRIVATE "${SOLOUD_DIR}/include")

# Tell SoLoud which backend to use. miniaudio has zero external deps.
target_compile_definitions(${PLUGIN_NAME}                               PRIVATE WITH_MINIAUDIO=1)
target_compile_definitions(${PLUGIN_NAME}_static                        PRIVATE WITH_MINIAUDIO=1)



print_source_file_details(
    SOURCES                                                             ${PLUGIN_SOURCES}
    NAME                                                                ${PLUGIN_NAME}
    SOURCE_DIR                                                          "${CMAKE_CURRENT_SOURCE_DIR}"
    VERBOSE                                                             CORE_VERBOSE_INFO
    TARGET                                                              ${PLUGIN_NAME}
)
