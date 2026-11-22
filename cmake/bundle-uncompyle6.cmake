cmake_minimum_required(VERSION 3.20)

# Invoked as a build-time CMake script, never by GitHub Actions or pip.
foreach(required IN ITEMS EVECC_SOURCE_DIR EVECC_OUTPUT_DIR EVECC_WHEEL_CACHE)
    if(NOT DEFINED ${required} OR "${${required}}" STREQUAL "")
        message(FATAL_ERROR "Missing -D${required}=... for uncompyle6 bundling")
    endif()
endforeach()

include("${CMAKE_CURRENT_LIST_DIR}/uncompyle6-wheels.cmake")
get_filename_component(EVECC_SOURCE_DIR "${EVECC_SOURCE_DIR}" ABSOLUTE)
get_filename_component(EVECC_OUTPUT_DIR "${EVECC_OUTPUT_DIR}" ABSOLUTE)
get_filename_component(EVECC_WHEEL_CACHE "${EVECC_WHEEL_CACHE}" ABSOLUTE)

set(stage "${EVECC_OUTPUT_DIR}/_uncompyle6_stage")
file(MAKE_DIRECTORY "${EVECC_WHEEL_CACHE}" "${EVECC_OUTPUT_DIR}")
file(REMOVE_RECURSE "${stage}")
file(MAKE_DIRECTORY "${stage}")

set(packages xdis spark_parser click)
foreach(spec IN LISTS EVECC_WHEELS)
    string(REPLACE "|" ";" fields "${spec}")
    list(LENGTH fields field_count)
    if(NOT field_count EQUAL 4)
        message(FATAL_ERROR "Incorrect wheel manifest: ${spec}")
    endif()
    list(GET fields 0 name)
    list(GET fields 1 archive_name)
    list(GET fields 2 expected_sha)
    list(GET fields 3 url)
    set(archive "${EVECC_WHEEL_CACHE}/${archive_name}")

    if(EXISTS "${archive}")
        file(SHA256 "${archive}" got_sha)
        if(NOT got_sha STREQUAL expected_sha)
            message(FATAL_ERROR
                "Cached ${archive_name} has wrong SHA256. Delete it and retry.")
        endif()
        message(STATUS "Using verified cached ${archive_name}")
    else()
        message(STATUS "Fetching pinned Python 2 wheel: ${archive_name}")
        set(partial "${archive}.partial")
        file(REMOVE "${partial}")
        file(DOWNLOAD "${url}" "${partial}"
            EXPECTED_HASH "SHA256=${expected_sha}"
            TLS_VERIFY ON
            STATUS download_status
            TIMEOUT 120 INACTIVITY_TIMEOUT 30)
        list(GET download_status 0 rc)
        if(NOT rc EQUAL 0)
            list(GET download_status 1 reason)
            file(REMOVE "${partial}")
            message(FATAL_ERROR
                "Could not download ${archive_name}: ${reason}. "
                "For offline builds, place a verified wheel in "
                "${EVECC_WHEEL_CACHE} and rerun.")
        endif()
        file(RENAME "${partial}" "${archive}")
    endif()

    # Wheels are zip files with pure-Python source and license metadata.
    # SHA256 is checked before extraction so the build cannot silently
    # import a changed third-party package.
    file(ARCHIVE_EXTRACT INPUT "${archive}" DESTINATION "${stage}")
endforeach()

set(upstream "${EVECC_SOURCE_DIR}/vendor/uncompyle6")
if(NOT EXISTS "${upstream}/uncompyle6/__init__.py"
   OR NOT EXISTS "${upstream}/COPYING")
    message(FATAL_ERROR "Missing pinned uncompyle6 git submodule or its COPYING license")
endif()
file(COPY "${upstream}/uncompyle6" DESTINATION "${stage}")
file(MAKE_DIRECTORY "${stage}/LICENSES")
file(COPY "${upstream}/COPYING" DESTINATION "${stage}/LICENSES")
file(RENAME "${stage}/LICENSES/COPYING"
    "${stage}/LICENSES/uncompyle6-GPL-3.0.txt")

foreach(expected IN ITEMS
    "uncompyle6/__init__.py"
    "xdis/__init__.py"
    "spark_parser/__init__.py"
    "click/__init__.py"
    "six.py")
    if(NOT EXISTS "${stage}/${expected}")
        message(FATAL_ERROR "Bundled dependency is missing: ${expected}")
    endif()
endforeach()

# Include wheel dist-info directories so all installed package metadata and
# third-party licenses remain with the self-contained decompiler.
file(GLOB metadata_dirs RELATIVE "${stage}" "${stage}