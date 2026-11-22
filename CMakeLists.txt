cmake_minimum_required(VERSION 3.20)
project(evecc LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT WIN32 AND NOT CMAKE_SYSTEM_NAME STREQUAL "Linux")
    message(FATAL_ERROR "EVECC currently supports native Windows and Linux only; macOS/BSD need a separate crypto backend")
endif()

option(EVECC_BUILD_PACKAGE "Create a portable distribution archive" ON)
set(PYTHON_VERSION "2.7.13" CACHE STRING "EVE-compatible embedded Python runtime")
if(NOT PYTHON_VERSION STREQUAL "2.7.13")
    message(FATAL_ERROR "This version of evecc requires Python 2.7.13")
endif()
set(BUILD_LIBPYTHON_SHARED OFF CACHE BOOL "Embed Python statically" FORCE)
set(BUILD_EXTENSIONS_AS_BUILTIN ON CACHE BOOL "Bundle Python modules" FORCE)
set(BUILTIN_UNICODEDATA ON CACHE BOOL "Bundle unicode tables" FORCE)
set(BUILD_SHARED_LIBS OFF)
set(CRC32C_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(CRC32C_BUILD_BENCHMARKS OFF CACHE BOOL "" FORCE)
set(CRC32C_USE_GLOG OFF CACHE BOOL "" FORCE)
set(LOGURU_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(LOGURU_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(LOGURU_INSTALL OFF CACHE BOOL "" FORCE)
# Python 2.7's _curses directly accesses ncurses WINDOW internals removed
# from recent ncurses headers. The optional extension is not used by evecc.
if(UNIX AND NOT APPLE)
    set(ENABLE_CURSES OFF CACHE BOOL "Disable incompatible Python 2.7 curses" FORCE)
    set(ENABLE_CURSES_PANEL OFF CACHE BOOL "Disable incompatible Python 2.7 curses panel" FORCE)
    set(ENABLE_SSL OFF CACHE BOOL "Disable incompatible Python 2.7 OpenSSL extension" FORCE)
    set(ENABLE_HASHLIB OFF CACHE BOOL "Disable incompatible Python 2.7 OpenSSL hashing extension" FORCE)
endif()

foreach(path IN ITEMS
    vendor/crc32c/CMakeLists.txt
    vendor/loguru/CMakeLists.txt
    vendor/python-cmake-buildsystem/CMakeLists.txt
    vendor/inih/ini.c
    vendor/uncompyle2/uncompyle2/__init__.py
    vendor/uncompyle6/uncompyle6/__init__.py)
    if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/${path}")
        message(FATAL_ERROR "Missing ${path}; run git submodule update --init --recursive")
    endif()
endforeach()
add_subdirectory(vendor/crc32c)
add_subdirectory(vendor/loguru)
add_subdirectory(vendor/python-cmake-buildsystem)
# Python 2.7.13's posixmodule.c expects makedev/major/minor to be defined
# transitively by sys/types.h; modern glibc moved them to sys/sysmacros.h.
if(UNIX AND NOT APPLE)
    target_compile_options(libpython-static PRIVATE
        "$<$<COMPILE_LANGUAGE:C>:-include>"
        "$<$<COMPILE_LANGUAGE:C>:sys/sysmacros.h>")
endif()

find_package(ZLIB REQUIRED)
if(UNIX AND NOT APPLE)
    find_package(OpenSSL REQUIRED)
endif()

set(evecc_sources
    src/main.cpp src/blue_module.cpp src/bluecrypto.cpp
    src/util.cpp src/commands.cpp src/py_build_cache.cpp
    vendor/inih/ini.c vendor/inih/cpp/INIReader.cpp
)
if(UNIX AND NOT APPLE)
    list(REMOVE_ITEM evecc_sources src/bluecrypto.cpp)
    list(APPEND evecc_sources src/bluecrypto_linux.cpp)
endif()

add_executable(evecc ${evecc_sources})
target_include_directories(evecc PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
    "${CMAKE_CURRENT_SOURCE_DIR}/vendor"
    "${CMAKE_CURRENT_BINARY_DIR}/gen"
    "${CMAKE_CURRENT_BINARY_DIR}/vendor/Python-2.7.13/Include"
    "${CMAKE_CURRENT_BINARY_DIR}/vendor/python-cmake-buildsystem/include"
    "${CMAKE_CURRENT_BINARY_DIR}/vendor/python-cmake-buildsystem/bin"
    "${CMAKE_CURRENT_BINARY_DIR}/vendor/python-cmake-buildsystem/bin/PC"
)
target_link_libraries(evecc PRIVATE libpython-static crc32c loguru ZLIB::ZLIB)
if(UNIX AND NOT APPLE)
    target_link_libraries(evecc PRIVATE OpenSSL::Crypto)
endif()
if(WIN32)
    target_include_directories(evecc PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/vendor/Python-2.7.13/PC")
    target_compile_definitions(evecc PRIVATE _CRT_SECURE_NO_WARNINGS)
    target_link_libraries(evecc PRIVATE
        advapi32 ws2_32 user32 shell32 ole32 oleaut32 rpcrt4
        msi cabinet)
endif()

find_package(Git QUIET)
set(GIT_HASH "unknown")
if(GIT_FOUND)
    execute_process(
        COMMAND "${GIT_EXECUTABLE}" describe --tags --always
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
        OUTPUT_VARIABLE GIT_HASH OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET)
endif()
file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/gen")
configure_file(cmake/version.h.in "${CMAKE_CURRENT_BINARY_DIR}/gen/version.h" @ONLY)

set(EVECC_PYTHON_LIB_SOURCE
    "${CMAKE_BINARY_DIR}/vendor/Python-2.7.13/Lib" CACHE PATH
    "Location of the Python 2.7.13 standard-library sources")
set(EVECC_PYTHON_DIR "${CMAKE_CURRENT_BINARY_DIR}/Python")
# Python's native Linux startup locates the encodings package under
# <PYTHONHOME>/lib/python2.7 (not Windows' <PYTHONHOME>/Lib).
if(UNIX AND NOT APPLE)
    set(EVECC_LIB_DIR "${EVECC_PYTHON_DIR}/lib/python2.7")
else()
    set(EVECC_LIB_DIR "${EVECC_PYTHON_DIR}/Lib")
endif()
set(EVECC_LIB_STAMP "${EVECC_LIB_DIR}/.evecc-ready")
add_custom_command(OUTPUT "${EVECC_LIB_STAMP}"
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${EVECC_LIB_DIR}"
    COMMAND "${CMAKE_COMMAND}" -E copy_directory
        "${EVECC_PYTHON_LIB_SOURCE}" "${EVECC_LIB_DIR}"
    COMMAND "${CMAKE_COMMAND}" -E touch "${EVECC_LIB_STAMP}"
    DEPENDS python
    COMMENT "Bundling native Python 2.7 standard library"
    VERBATIM)

add_custom_command(OUTPUT "${EVECC_PYTHON_DIR}/uncompyle2.zip"
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${EVECC_PYTHON_DIR}"
    COMMAND "${CMAKE_COMMAND}" -E tar cf
        "${EVECC_PYTHON_DIR}/uncompyle2.zip" --format=zip uncompyle2
    WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/vendor/uncompyle2"
    DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/vendor/uncompyle2/uncompyle2"
    VERBATIM)
set(EVECC_WHEEL_CACHE "${CMAKE_BINARY_DIR}/wheel-cache" CACHE PATH
    "Directory for SHA256-verified, cached Python 2 decompiler wheels")
file(GLOB_RECURSE EVECC_UNCOMPYLE6_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/vendor/uncompyle6/uncompyle6/*.py")
add_custom_command(OUTPUT "${EVECC_PYTHON_DIR}/uncompyle6.zip"
    COMMAND "${CMAKE_COMMAND}"
        "-DEVECC_SOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}"
        "-DEVECC_OUTPUT_DIR=${EVECC_PYTHON_DIR}"
        "-DEVECC_WHEEL_CACHE=${EVECC_WHEEL_CACHE}"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/bundle-uncompyle6.cmake"
    DEPENDS ${EVECC_UNCOMPYLE6_SOURCES}
        "${CMAKE_CURRENT_SOURCE_DIR}/cmake/bundle-uncompyle6.cmake"
        "${CMAKE_CURRENT_SOURCE_DIR}/cmake/uncompyle6-wheels.cmake"
    COMMENT "Bundling Python 2 uncompyle6 with pinned xdis, spark-parser, click and six"
    VERBATIM)
add_custom_target(evecc_python_dependencies ALL DEPENDS
    "${EVECC_LIB_STAMP}"
    "${EVECC_PYTHON_DIR}/uncompyle2.zip"
    "${EVECC_PYTHON_DIR}/uncompyle6.zip")
add_dependencies(evecc evecc_python_dependencies)

include(GNUInstallDirs)
install(TARGETS evecc RUNTIME DESTINATION ".")
install(DIRECTORY "${EVECC_PYTHON_DIR}/" DESTINATION "Python")
install(FILES readme.md LICENSE DESTINATION ".")
install(FILES docs/linux.md DESTINATION "docs")
if(EVECC_BUILD_PACKAGE)
    set(CPACK_PACKAGE_NAME "evecc")
    set(CPACK_PACKAGE_VERSION "0.1.0")
    set(CPACK_PACKAGE_FILE_NAME "evecc-${CMAKE_SYSTEM_NAME}-${CMAKE_SYSTEM_PROCESSOR}")
    set(CPACK_GENERATOR "TGZ")
    include(CPack)
endif()
