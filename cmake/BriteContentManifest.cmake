# Writes a content manifest (modules/content/include/Content/Manifest.hpp): every file a build shipped, with its size
# and SHA-256. Run by brite_content_manifest (cmake/BriteContent.cmake) at BUILD time, after every file it names is in
# place, as `cmake -D... -P BriteContentManifest.cmake`.
#
# Inputs:
#   OUTPUT          the manifest to write
#   ROOT            the folder every listed path is relative to
#   APP             the application's name
#   VERSION         the version string, or
#   VERSION_FILE    a file whose first line is the version (wins over VERSION when given)
#   SEQUENCE        a whole number, 0 if not given
#   PROPERTIES_FILE a file of `key=value` lines (optional)
#   LISTS_FILE      a file naming, one per line, the files that list the paths (each relative to ROOT, one per line)
#
# The text is exactly what Manifest::ToJson writes for the same manifest -- keys in a fixed order, files and properties
# in byte order, two spaces of indent -- so the same files always give the same bytes. A path that is no content name,
# two paths that are one file ignoring case, or a listed file that is not there stops the build: a manifest never
# names what was not shipped.

# A script run with -P sets no policies of its own: without this, a CMake before 4 runs it with the OLD behaviour of
# every policy, and `if(... IN_LIST ...)` (CMP0057) is "unknown arguments". The engine's own minimum.
cmake_minimum_required(VERSION 3.20)

foreach(required OUTPUT ROOT APP LISTS_FILE)
    if(NOT DEFINED ${required})
        message(FATAL_ERROR "BriteContentManifest.cmake: -D${required}=... is required")
    endif()
endforeach()

# One JSON string: quotes and backslashes escaped; anything but printable ASCII is refused rather than escaped, and so
# is '@', which the file(CONFIGURE ... @ONLY) that writes the manifest would read as a variable to substitute.
function(brite_json_string text out)
    if(text MATCHES "[^ -~]")
        message(FATAL_ERROR "BriteContentManifest.cmake: a character that is not printable ASCII in \"${text}\"")
    endif()
    if(text MATCHES "@")
        message(FATAL_ERROR "BriteContentManifest.cmake: an '@' in \"${text}\"")
    endif()
    string(REPLACE "\\" "\\\\" text "${text}")
    string(REPLACE "\"" "\\\"" text "${text}")
    set(${out} "\"${text}\"" PARENT_SCOPE)
endfunction()

set(version "${VERSION}")
if(DEFINED VERSION_FILE AND NOT VERSION_FILE STREQUAL "")
    if(NOT EXISTS "${VERSION_FILE}")
        message(FATAL_ERROR "BriteContentManifest.cmake: no version file at ${VERSION_FILE}")
    endif()
    file(STRINGS "${VERSION_FILE}" version_lines LIMIT_COUNT 1)
    list(GET version_lines 0 version)
    string(STRIP "${version}" version)
endif()
if(NOT DEFINED SEQUENCE OR SEQUENCE STREQUAL "")
    set(SEQUENCE 0)
endif()
if(NOT SEQUENCE MATCHES "^[0-9]+$")
    message(FATAL_ERROR "BriteContentManifest.cmake: SEQUENCE is not a whole number: ${SEQUENCE}")
endif()

# The paths: every list, deduplicated, each a content name, none twice ignoring case.
set(paths "")
file(STRINGS "${LISTS_FILE}" list_files)
foreach(list_file IN LISTS list_files)
    if(list_file STREQUAL "")
        continue()
    endif()
    if(NOT EXISTS "${list_file}")
        message(FATAL_ERROR "BriteContentManifest.cmake: no list at ${list_file}")
    endif()
    file(STRINGS "${list_file}" lines)
    foreach(line IN LISTS lines)
        string(STRIP "${line}" line)
        if(NOT line STREQUAL "")
            list(APPEND paths "${line}")
        endif()
    endforeach()
endforeach()
list(REMOVE_DUPLICATES paths)
list(SORT paths COMPARE STRING)
set(folded_seen "")
foreach(path IN LISTS paths)
    if(NOT path MATCHES "^[A-Za-z0-9_+.-]+(/[A-Za-z0-9_+.-]+)*$" OR path MATCHES "(^|/)\\.\\.?(/|$)" OR
       path MATCHES "\\.(/|$)")
        message(FATAL_ERROR "BriteContentManifest.cmake: \"${path}\" is not a content name")
    endif()
    string(TOLOWER "${path}" folded)
    if(folded IN_LIST folded_seen)
        message(FATAL_ERROR "BriteContentManifest.cmake: \"${path}\" is listed twice (ignoring case)")
    endif()
    list(APPEND folded_seen "${folded}")
endforeach()

# The properties: key=value lines, each key once, written in key order -- the order of the KEYS, as ToJson writes them
# (sorting whole lines would put "a2=2" before "a=1", since '2' sorts before '=').
set(property_lines "")
if(DEFINED PROPERTIES_FILE AND NOT PROPERTIES_FILE STREQUAL "")
    file(STRINGS "${PROPERTIES_FILE}" property_lines)
endif()
set(property_keys "")
set(property_values "")
foreach(line IN LISTS property_lines)
    string(FIND "${line}" "=" equals)
    if(equals LESS 1)
        message(FATAL_ERROR "BriteContentManifest.cmake: a property is not key=value: \"${line}\"")
    endif()
    string(SUBSTRING "${line}" 0 ${equals} key)
    math(EXPR value_start "${equals} + 1")
    string(SUBSTRING "${line}" ${value_start} -1 value)
    if(value STREQUAL "")
        message(FATAL_ERROR "BriteContentManifest.cmake: the property \"${key}\" has no value")
    endif()
    if(key IN_LIST property_keys)
        message(FATAL_ERROR "BriteContentManifest.cmake: the property \"${key}\" is given twice")
    endif()
    list(APPEND property_keys "${key}")
    list(APPEND property_values "${value}")
endforeach()
set(sorted_keys ${property_keys})
list(SORT sorted_keys COMPARE STRING)

brite_json_string("${APP}" app_json)
brite_json_string("${version}" version_json)
set(json "{\n  \"app\": ${app_json},\n  \"version\": ${version_json},\n  \"sequence\": ${SEQUENCE},\n")
if(sorted_keys STREQUAL "")
    string(APPEND json "  \"properties\": {},\n")
else()
    string(APPEND json "  \"properties\": {\n")
    set(first TRUE)
    foreach(key IN LISTS sorted_keys)
        list(FIND property_keys "${key}" at)
        list(GET property_values ${at} value)
        brite_json_string("${key}" key_json)
        brite_json_string("${value}" value_json)
        if(NOT first)
            string(APPEND json ",\n")
        endif()
        string(APPEND json "    ${key_json}: ${value_json}")
        set(first FALSE)
    endforeach()
    string(APPEND json "\n  },\n")
endif()
if(paths STREQUAL "")
    string(APPEND json "  \"files\": []\n}\n")
else()
    string(APPEND json "  \"files\": [\n")
    set(first TRUE)
    foreach(path IN LISTS paths)
        set(file "${ROOT}/${path}")
        if(NOT EXISTS "${file}" OR IS_DIRECTORY "${file}")
            message(FATAL_ERROR "BriteContentManifest.cmake: the manifest names ${path}, which is not in ${ROOT}")
        endif()
        file(SIZE "${file}" size)
        file(SHA256 "${file}" sha256)
        if(NOT first)
            string(APPEND json ",\n")
        endif()
        string(APPEND json "    {\n      \"path\": \"${path}\",\n      \"size\": ${size},\n      \"sha256\": \"${sha256}\"\n    }")
        set(first FALSE)
    endforeach()
    string(APPEND json "\n  ]\n}\n")
endif()

# Written only when it changed (file(CONFIGURE) compares first), so an unchanged build leaves the file -- and its
# time -- alone; with LF line ends on every platform, as ToJson writes them (file(WRITE) writes CRLF on Windows).
# @ONLY: the text holds no @, so nothing in it is substituted.
file(CONFIGURE OUTPUT "${OUTPUT}" CONTENT "${json}" @ONLY NEWLINE_STYLE UNIX)
