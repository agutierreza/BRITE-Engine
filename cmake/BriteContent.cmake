# The build half of the content module (modules/content): files an executable ships, staged beside it, and the
# manifest that names them.
#
#   brite_add_content(<name> FROM <folder> INTO <subfolder>
#                     [PATTERNS <glob>...] [EXCLUDE <regex>...] [DESTINATION <folder>])
#
# A CONTENT SET: every file under FROM matching PATTERNS (default: every file), except those whose path under FROM
# matches an EXCLUDE regex, staged into DESTINATION/INTO (DESTINATION defaults to the folder executables are built
# into), one copy per file, by a custom target <name>. Anything that needs the files -- an executable, a test --
# depends on <name>. Each file's content name, INTO/<path under FROM>, is recorded on <name>; a file whose name is no
# content name stops the configure.
#
#   brite_content_manifest(<executable> APP <name> CONTENT <content set>...
#                          [VERSION <string> | VERSION_FILE <file>] [SEQUENCE <n>] [PROPERTIES <key=value>...]
#                          [EXTRA_LISTS <file>...] [OUTPUT_NAME <file name>])
#
# What the executable ships, in <executable>.manifest.json (or OUTPUT_NAME) beside it, written at build time whenever
# the executable or any of its files changes: the executable itself, every file of the CONTENT sets (which it is made
# to depend on), and every path the EXTRA_LISTS files name (one per line, relative to the executable's folder -- files
# some other build step places, such as runtime libraries; give such a list as the BYPRODUCTS of the step that writes
# it). VERSION_FILE is read at build time, so a version a build step writes is the one recorded.
# ContentRoot::FromFolder reads the result.

set(BRITE_CONTENT_MANIFEST_SCRIPT "${CMAKE_CURRENT_LIST_DIR}/BriteContentManifest.cmake" CACHE INTERNAL
    "The script brite_content_manifest runs")

# The folder executables are built into: the target's own when given, else the directory's.
function(brite_content_output_folder target out)
    set(folder "")
    if(target)
        get_target_property(folder ${target} RUNTIME_OUTPUT_DIRECTORY)
    endif()
    if(NOT folder)
        if(CMAKE_RUNTIME_OUTPUT_DIRECTORY)
            set(folder "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}")
        elseif(target)
            get_target_property(folder ${target} BINARY_DIR)
        else()
            set(folder "${CMAKE_CURRENT_BINARY_DIR}")
        endif()
    endif()
    set(${out} "${folder}" PARENT_SCOPE)
endfunction()

function(brite_add_content name)
    cmake_parse_arguments(PARSE_ARGV 1 ARG "" "FROM;INTO;DESTINATION" "PATTERNS;EXCLUDE")
    foreach(required FROM INTO)
        if(NOT ARG_${required})
            message(FATAL_ERROR "brite_add_content(${name}): ${required} is required")
        endif()
    endforeach()
    if(NOT ARG_PATTERNS)
        set(ARG_PATTERNS "*")
    endif()
    if(NOT ARG_DESTINATION)
        brite_content_output_folder("" ARG_DESTINATION)
    endif()
    get_filename_component(from "${ARG_FROM}" ABSOLUTE)

    set(globs "")
    foreach(pattern IN LISTS ARG_PATTERNS)
        list(APPEND globs "${from}/${pattern}")
    endforeach()
    file(GLOB_RECURSE sources CONFIGURE_DEPENDS ${globs})
    list(SORT sources)

    set(staged_files "")
    set(names "")
    foreach(source IN LISTS sources)
        file(RELATIVE_PATH relative "${from}" "${source}")
        set(skip FALSE)
        foreach(exclude IN LISTS ARG_EXCLUDE)
            if(relative MATCHES "${exclude}")
                set(skip TRUE)
            endif()
        endforeach()
        if(skip)
            continue()
        endif()
        set(content_name "${ARG_INTO}/${relative}")
        if(NOT content_name MATCHES "^[A-Za-z0-9_+.-]+(/[A-Za-z0-9_+.-]+)*$" OR content_name MATCHES "\\.(/|$)")
            message(FATAL_ERROR "brite_add_content(${name}): \"${content_name}\" is not a content name")
        endif()
        set(staged "${ARG_DESTINATION}/${content_name}")
        add_custom_command(OUTPUT "${staged}"
            COMMAND ${CMAKE_COMMAND} -E copy "${source}" "${staged}"
            DEPENDS "${source}"
            COMMENT "Staging ${content_name}"
            VERBATIM)
        list(APPEND staged_files "${staged}")
        list(APPEND names "${content_name}")
    endforeach()

    add_custom_target(${name} ALL DEPENDS ${staged_files})
    set_target_properties(${name} PROPERTIES BRITE_CONTENT_NAMES "${names}" BRITE_CONTENT_STAGED "${staged_files}"
                                             BRITE_CONTENT_DESTINATION "${ARG_DESTINATION}")
endfunction()

function(brite_content_manifest target)
    cmake_parse_arguments(PARSE_ARGV 1 ARG "" "APP;VERSION;VERSION_FILE;SEQUENCE;OUTPUT_NAME"
                          "CONTENT;PROPERTIES;EXTRA_LISTS")
    if(NOT ARG_APP)
        message(FATAL_ERROR "brite_content_manifest(${target}): APP is required")
    endif()
    if(NOT ARG_OUTPUT_NAME)
        set(ARG_OUTPUT_NAME "${target}.manifest.json")
    endif()
    if(NOT ARG_SEQUENCE)
        set(ARG_SEQUENCE 0)
    endif()
    brite_content_output_folder(${target} folder)
    set(names "")
    set(staged "")
    foreach(set_name IN LISTS ARG_CONTENT)
        get_target_property(set_folder ${set_name} BRITE_CONTENT_DESTINATION)
        if(NOT set_folder)
            message(FATAL_ERROR "brite_content_manifest(${target}): ${set_name} is no content set (brite_add_content)")
        endif()
        if(NOT set_folder STREQUAL folder)
            message(FATAL_ERROR "brite_content_manifest(${target}): ${set_name} is staged into ${set_folder}, "
                                "not beside ${target} in ${folder}")
        endif()
        get_target_property(set_names ${set_name} BRITE_CONTENT_NAMES)
        get_target_property(set_staged ${set_name} BRITE_CONTENT_STAGED)
        list(APPEND names ${set_names})
        list(APPEND staged ${set_staged})
        add_dependencies(${target} ${set_name})
    endforeach()

    # The names, the executable's own among them, and the properties -- written at generate time, and only when they
    # change, so they are files the manifest's command can depend on.
    set(work "${CMAKE_CURRENT_BINARY_DIR}/brite_content")
    list(JOIN names "\n" names_text)
    file(GENERATE OUTPUT "${work}/${target}.names.txt" CONTENT "$<TARGET_FILE_NAME:${target}>\n${names_text}\n"
         NEWLINE_STYLE UNIX)
    list(JOIN ARG_PROPERTIES "\n" properties_text)
    file(GENERATE OUTPUT "${work}/${target}.properties.txt" CONTENT "${properties_text}\n" NEWLINE_STYLE UNIX)

    # And the lists to read, one per line: the names above and every extra list (never on a command line, where a
    # shell would see the separators).
    set(lists_text "${work}/${target}.names.txt")
    foreach(extra IN LISTS ARG_EXTRA_LISTS)
        get_filename_component(extra "${extra}" ABSOLUTE)
        string(APPEND lists_text "\n${extra}")
    endforeach()
    file(GENERATE OUTPUT "${work}/${target}.lists.txt" CONTENT "${lists_text}\n" NEWLINE_STYLE UNIX)
    set(output "${folder}/${ARG_OUTPUT_NAME}")
    add_custom_command(OUTPUT "${output}"
        COMMAND ${CMAKE_COMMAND}
            "-DOUTPUT=${output}" "-DROOT=${folder}" "-DAPP=${ARG_APP}" "-DVERSION=${ARG_VERSION}"
            "-DVERSION_FILE=${ARG_VERSION_FILE}" "-DSEQUENCE=${ARG_SEQUENCE}"
            "-DPROPERTIES_FILE=${work}/${target}.properties.txt" "-DLISTS_FILE=${work}/${target}.lists.txt"
            -P "${BRITE_CONTENT_MANIFEST_SCRIPT}"
        DEPENDS ${target} ${staged} "${work}/${target}.names.txt" "${work}/${target}.properties.txt"
                "${work}/${target}.lists.txt"
                ${ARG_EXTRA_LISTS} ${ARG_VERSION_FILE} "${BRITE_CONTENT_MANIFEST_SCRIPT}"
        COMMENT "Writing ${ARG_OUTPUT_NAME}"
        VERBATIM)
    add_custom_target(${target}Manifest ALL DEPENDS "${output}")
endfunction()
