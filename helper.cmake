# ----------------------------------------------------------------------
# helper.cmake
#
# Provides:
#   format_file_size(<bytes> <out_var>)
#   print_source_file_details(SOURCES <list> [VERBOSE <option>]
#                             [NAME <label>] [SOURCE_DIR <dir>]
#                             [TARGET <target>])
#
# Usage:
#   include(cmake/helper)
#   print_source_file_details(SOURCES ${MY_SOURCES} VERBOSE ${MY_OPTION}
#                             NAME "Core" SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}"
#                             TARGET my_target)
# ----------------------------------------------------------------------

include_guard(GLOBAL)


# Provides a reusable function to clone/fetch a Git repository into a vendor directory.
function(git_clone_or_update REPO_URL TARGET_DIR)
    set(options)
    set(oneValueArgs DEPTH BRANCH)
    set(multiValueArgs)
    cmake_parse_arguments(GCOU "${options}" "${oneValueArgs}" "${multiValueArgs}" ${ARGN})

    if(NOT DEFINED GCOU_DEPTH)
        set(GCOU_DEPTH 1)
    endif()

    find_package(Git REQUIRED)

    # Prepare branch string for logging
    if(GCOU_BRANCH)
        set(branch_msg " (branch: ${GCOU_BRANCH})")
    else()
        set(branch_msg "")
    endif()

    if(NOT EXISTS "${TARGET_DIR}")
        message(STATUS "Cloning ${REPO_URL}${branch_msg} into ${TARGET_DIR} ...")
        set(clone_cmd ${GIT_EXECUTABLE} clone --depth ${GCOU_DEPTH})
        if(GCOU_BRANCH)
            list(APPEND clone_cmd -b ${GCOU_BRANCH})
        endif()
        list(APPEND clone_cmd "${REPO_URL}" "${TARGET_DIR}")

        execute_process(
            COMMAND ${clone_cmd}
            RESULT_VARIABLE clone_result
            ERROR_VARIABLE  clone_error
        )
        if(NOT clone_result EQUAL 0)
            message(FATAL_ERROR "Failed to clone ${REPO_URL}: ${clone_error}")
        endif()
        message(STATUS "Repository cloned successfully into ${TARGET_DIR}")
    else()
        message(STATUS "Repository already present in ${TARGET_DIR}")
        if(GCOU_BRANCH)
            # Check current branch
            execute_process(
                COMMAND ${GIT_EXECUTABLE} -C "${TARGET_DIR}" rev-parse --abbrev-ref HEAD
                OUTPUT_VARIABLE current_branch
                OUTPUT_STRIP_TRAILING_WHITESPACE
                RESULT_VARIABLE branch_check_result
                ERROR_QUIET
            )
            if(branch_check_result EQUAL 0 AND NOT current_branch STREQUAL GCOU_BRANCH)
                message(STATUS "Switching from branch '${current_branch}' to '${GCOU_BRANCH}' ...")
                # Fetch the desired branch (shallow clone may not have it)
                execute_process(
                    COMMAND ${GIT_EXECUTABLE} -C "${TARGET_DIR}" fetch --depth ${GCOU_DEPTH} origin ${GCOU_BRANCH}
                    RESULT_VARIABLE fetch_result
                    ERROR_VARIABLE  fetch_error
                )
                if(NOT fetch_result EQUAL 0)
                    message(FATAL_ERROR "Failed to fetch branch ${GCOU_BRANCH}: ${fetch_error}")
                endif()
                # Checkout the branch
                execute_process(
                    COMMAND ${GIT_EXECUTABLE} -C "${TARGET_DIR}" checkout ${GCOU_BRANCH}
                    RESULT_VARIABLE checkout_result
                    ERROR_VARIABLE  checkout_error
                )
                if(NOT checkout_result EQUAL 0)
                    message(FATAL_ERROR "Failed to checkout branch ${GCOU_BRANCH}: ${checkout_error}")
                endif()
                message(STATUS "Now on branch '${GCOU_BRANCH}'")
            elseif(current_branch STREQUAL GCOU_BRANCH)
                message(STATUS "Already on correct branch '${GCOU_BRANCH}'")
            endif()
        endif()
    endif()
endfunction()


# ----------------------------------------------------------------------
# Helper: 
# ----------------------------------------------------------------------
function(compile_plugin TARGET_LIB_TYPE TARGET_NAME)
    # Derive base plugin name (strip "_static" suffix if present)
    string(REGEX REPLACE "_static$" "" BASE_NAME "${TARGET_NAME}")

    # Cache the source list per directory to avoid double globbing
    get_property(PLUGIN_SOURCES GLOBAL PROPERTY "${CMAKE_CURRENT_SOURCE_DIR}_SOURCES")
    if(NOT PLUGIN_SOURCES)
        file(GLOB_RECURSE PLUGIN_SOURCES
            CONFIGURE_DEPENDS
            "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp"
            "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cxx"
            "${CMAKE_CURRENT_SOURCE_DIR}/src/*.c"
        )
        set_property(GLOBAL PROPERTY "${CMAKE_CURRENT_SOURCE_DIR}_SOURCES" "${PLUGIN_SOURCES}")
    endif()

    add_library(${TARGET_NAME} ${TARGET_LIB_TYPE} ${PLUGIN_SOURCES})
    target_compile_definitions(${TARGET_NAME}       PRIVATE GLT_MODULE_NAME="${BASE_NAME}")
    target_include_directories(${TARGET_NAME}       PRIVATE
        ${CMAKE_SOURCE_DIR}/core/src/
        ${CMAKE_CURRENT_SOURCE_DIR}/src/
    )
    set_target_properties(${TARGET_NAME}            PROPERTIES
        LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin/$<CONFIG>/plugin/${BASE_NAME}"
        ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin/$<CONFIG>/plugin/${BASE_NAME}"
        OBJECT_OUTPUT_DIRECTORY  "${CMAKE_BINARY_DIR}/bin-int/$<CONFIG>/plugin/${BASE_NAME}"
    )
    target_link_options(${TARGET_NAME}              PRIVATE -rdynamic)
    # Link with any additional libraries passed as extra arguments
    if(ARGN)
        target_link_libraries(${TARGET_NAME}        PRIVATE ${ARGN})
    endif()

    if(MSVC)
        target_compile_options(${PLUGIN_NAME}       PRIVATE /W4)
    else()
        target_compile_options(${PLUGIN_NAME}       PRIVATE -Wall -Wextra -Wpedantic)
    endif()
endfunction()


# ----------------------------------------------------------------------
# Helper: format a byte count into a human-readable string (binary units)
# ----------------------------------------------------------------------
function(format_file_size SIZE_IN_BYTES OUT_VAR)
    if(SIZE_IN_BYTES GREATER_EQUAL 1073741824)       # 1 GiB
        math(EXPR VALUE_INT "${SIZE_IN_BYTES} / 1073741824")
        math(EXPR VALUE_REM "${SIZE_IN_BYTES} % 1073741824")
        math(EXPR VALUE_DEC "(${VALUE_REM} * 100) / 1073741824")
        if(VALUE_DEC LESS 10)
            set(VALUE_DEC "0${VALUE_DEC}")
        endif()
        set(${OUT_VAR} "${VALUE_INT}.${VALUE_DEC} GiB" PARENT_SCOPE)
    elseif(SIZE_IN_BYTES GREATER_EQUAL 1048576)      # 1 MiB
        math(EXPR VALUE_INT "${SIZE_IN_BYTES} / 1048576")
        math(EXPR VALUE_REM "${SIZE_IN_BYTES} % 1048576")
        math(EXPR VALUE_DEC "(${VALUE_REM} * 100) / 1048576")
        if(VALUE_DEC LESS 10)
            set(VALUE_DEC "0${VALUE_DEC}")
        endif()
        set(${OUT_VAR} "${VALUE_INT}.${VALUE_DEC} MiB" PARENT_SCOPE)
    elseif(SIZE_IN_BYTES GREATER_EQUAL 1024)         # 1 KiB
        math(EXPR VALUE_INT "${SIZE_IN_BYTES} / 1024")
        math(EXPR VALUE_REM "${SIZE_IN_BYTES} % 1024")
        math(EXPR VALUE_DEC "(${VALUE_REM} * 100) / 1024")
        if(VALUE_DEC LESS 10)
            set(VALUE_DEC "0${VALUE_DEC}")
        endif()
        set(${OUT_VAR} "${VALUE_INT}.${VALUE_DEC} KiB" PARENT_SCOPE)
    else()                                           # bytes
        set(${OUT_VAR} "${SIZE_IN_BYTES} B" PARENT_SCOPE)
    endif()
endfunction()


# ----------------------------------------------------------------------
# Helper: simple separator line
# ----------------------------------------------------------------------
function(print_separator CHAR)
    if(NOT CHAR)
        set(CHAR "-")
    endif()
    string(RANDOM LENGTH 70 ALPHABET "${CHAR}${CHAR}${CHAR}${CHAR}${CHAR}" sep)
    message(STATUS "${sep}")
endfunction()


# ----------------------------------------------------------------------
# Main function: print statistics & optionally add build-time messages
# ----------------------------------------------------------------------
function(print_source_file_details)
    set(options)
    set(oneValueArgs VERBOSE NAME SOURCE_DIR TARGET)
    set(multiValueArgs SOURCES)
    cmake_parse_arguments(ARG "${options}" "${oneValueArgs}" "${multiValueArgs}" ${ARGN})

    if(NOT ARG_SOURCES)
        message(WARNING "print_source_file_details: SOURCES list is empty, nothing to do.")
        return()
    endif()

    # Defaults
    if(NOT ARG_NAME)
        set(ARG_NAME "Sources")
    endif()
    if(NOT ARG_SOURCE_DIR)
        set(ARG_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}")
    endif()

    # Verbose flag
    if(ARG_VERBOSE AND NOT ${ARG_VERBOSE})
        set(verbose OFF)
    else()
        set(verbose ON)
    endif()


    # ---- Always compute the summary ----
    list(LENGTH ARG_SOURCES num_files)
    set(total_size 0)
    foreach(file ${ARG_SOURCES})
        file(SIZE ${file} file_size)
        math(EXPR total_size "${total_size} + ${file_size}")
    endforeach()
    format_file_size(${total_size} total_formatted)

    # Print summary during configuration
    message(STATUS "${summary_msg}")

    # ---- Gather and print detailed list (verbose only) ----
    set(detail_lines)   # will hold the aligned text lines (without header/footer)

    if(verbose)
        # Pre‑compute per‑file formatted sizes
        foreach(file ${ARG_SOURCES})
            file(SIZE ${file} file_size)
            format_file_size(${file_size} formatted)
            string(MAKE_C_IDENTIFIER "${file}" safe_key)
            set("SIZE_${safe_key}" "${formatted}")
        endforeach()

        find_program(WC_EXECUTABLE wc)

        if(WC_EXECUTABLE)
            execute_process(
                COMMAND ${WC_EXECUTABLE} -l ${ARG_SOURCES}
                OUTPUT_VARIABLE wc_output
                RESULT_VARIABLE wc_result
                ERROR_QUIET
            )
        else()
            set(wc_result 1)   # cause fallback
        endif()

        if(WC_EXECUTABLE AND wc_result EQUAL 0)
            # First pass: collect entries and column widths
            set(entries)
            set(max_lc_len 0)
            set(max_fsize_len 0)

            string(REGEX REPLACE "\n$" "" wc_output "${wc_output}")
            string(REPLACE "\n" ";" wc_lines "${wc_output}")

            foreach(line ${wc_lines})
                string(STRIP "${line}" line)
                if(line MATCHES "^[0-9]+ total$")
                    continue()
                endif()
                if(line MATCHES "^([0-9]+) (.*)$")
                    set(lc "${CMAKE_MATCH_1}")
                    set(fpath "${CMAKE_MATCH_2}")

                    file(RELATIVE_PATH rel "${ARG_SOURCE_DIR}" "${fpath}")

                    string(MAKE_C_IDENTIFIER "${fpath}" key)
                    if(DEFINED SIZE_${key})
                        set(fsize "${SIZE_${key}}")
                    else()
                        set(fsize "?")
                    endif()

                    list(APPEND entries "${lc}|${fsize}|${rel}")

                    string(LENGTH "${lc}" lc_len)
                    if(lc_len GREATER max_lc_len)
                        set(max_lc_len ${lc_len})
                    endif()
                    string(LENGTH "${fsize}" fsize_len)
                    if(fsize_len GREATER max_fsize_len)
                        set(max_fsize_len ${fsize_len})
                    endif()
                endif()
            endforeach()

            # Second pass: build aligned lines
            set(detail_lines)
            foreach(entry ${entries})
                string(REPLACE "|" ";" parts "${entry}")
                list(GET parts 0 lc)
                list(GET parts 1 fsize)
                list(GET parts 2 rel)

                # Pad line count
                string(LENGTH "${lc}" lc_len)
                math(EXPR lc_pad "${max_lc_len} - ${lc_len}")
                set(lc_padded "")
                if(lc_pad GREATER 0)
                    foreach(i RANGE 1 ${lc_pad})
                        set(lc_padded "${lc_padded} ")
                    endforeach()
                endif()
                set(lc_padded "${lc_padded}${lc}")

                # Pad file size
                string(LENGTH "${fsize}" fsize_len)
                math(EXPR fsize_pad "${max_fsize_len} - ${fsize_len}")
                set(fsize_padded "")
                if(fsize_pad GREATER 0)
                    foreach(i RANGE 1 ${fsize_pad})
                        set(fsize_padded "${fsize_padded} ")
                    endforeach()
                endif()
                set(fsize_padded "${fsize_padded}${fsize}")

                list(APPEND detail_lines "  ${lc_padded} lines   ${fsize_padded}   ${rel}")
            endforeach()

        else()   # fallback: no wc → size‑only list
            if(NOT WC_EXECUTABLE)
                set(fallback_note " (wc not found - showing sizes only)")
            else()
                set(fallback_note " (wc failed - showing sizes only)")
            endif()

            set(max_fsize_len 0)
            foreach(file ${ARG_SOURCES})
                string(MAKE_C_IDENTIFIER "${file}" key)
                if(DEFINED SIZE_${key})
                    string(LENGTH "${SIZE_${key}}" len)
                    if(len GREATER max_fsize_len)
                        set(max_fsize_len ${len})
                    endif()
                endif()
            endforeach()

            set(detail_lines)
            foreach(file ${ARG_SOURCES})
                file(RELATIVE_PATH rel_file "${ARG_SOURCE_DIR}" "${file}")
                string(MAKE_C_IDENTIFIER "${file}" key)
                if(DEFINED SIZE_${key})
                    set(fsize "${SIZE_${key}}")
                else()
                    file(SIZE ${file} file_size)
                    format_file_size(${file_size} fsize)
                endif()
                string(LENGTH "${fsize}" len)
                math(EXPR pad "${max_fsize_len} - ${len}")
                set(padded "")
                if(pad GREATER 0)
                    foreach(i RANGE 1 ${pad})
                        set(padded "${padded} ")
                    endforeach()
                endif()
                set(padded "${padded}${fsize}")
                list(APPEND detail_lines "  ${padded}   ${rel_file}")
            endforeach()

            # prepend the fallback note as a line
            list(PREPEND detail_lines "${fallback_note}")
        endif()

        # ---- Print detailed list at configure time ----
        message(STATUS "-------------------------------------------------------------------------------------------------")
        message(STATUS " [${ARG_NAME}] source file details: ${num_files} source files, total size ${total_formatted}")

        foreach(line ${detail_lines})
            message(STATUS "${line}")
        endforeach()
        message(STATUS "-------------------------------------------------------------------------------------------------")
    endif()

    # ---- Setup build-time messages if a target was specified ----
    if(ARG_TARGET)
        if(NOT TARGET ${ARG_TARGET})
            message(FATAL_ERROR "print_source_file_details: '${ARG_TARGET}' is not a valid target")
        endif()

        # Compute required padding to make the message exactly 100 characters
        string(LENGTH "${ARG_NAME}" name_len)
        math(EXPR padding_len "100 - 30 - ${name_len}")   # 30 = length of "-- ========== " + " built successfully "
        if(padding_len LESS 0)
            set(padding_len 0)   # name too long, just no extra padding
        endif()
        string(REPEAT "=" ${padding_len} padding_equals)

        # Post-build success message with computed padding
        add_custom_command(TARGET ${ARG_TARGET} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E echo "-- ====== ${ARG_NAME} built successfully ${padding_equals}"
        )
    endif()
endfunction()
