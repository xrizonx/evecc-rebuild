cmake_minimum_required(VERSION 3.20)
project(evecc LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT WIN32 AND NOT UNIX)
    message(FATAL_ERROR "evecc supports native Windows and Linux builds")
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

foreach(submodule IN ITEMS crc32c loguru python-cmake-buildsystem uncompyle2 uncompyle6 inih)
    if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/vendor/${submodule}")
        message(FATAL_ERROR "Missing ${submodule}; run git submodule update --init --recursive")
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
set(EVECC_LIB_STAMP "${EVECC_PYTHON_DIR}/Lib/.evecc-ready")
add_custom_command(OUTPUT "${EVECC_LIB_STAMP}"
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${EVECC_PYTHON_DIR}/Lib"
    COMMAND "${CMAKE_COMMAND}" -E copy_directory
        "${EVECC_PYTHON_LIB_SOURCE}" "${EVECC_PYTHON_DIR}/Lib"
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
add_custom_command(OUTPUT "${EVECC_PYTHON_DIR}/uncompyle6.zip"
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${EVECC_PYTHON_DIR}"
    COMMAND "${CMAKE_COMMAND}" -E tar cf
        "${EVECC_PYTHON_DIR}/uncompyle6.zip" --format=zip uncompyle6
    WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/vendor/uncompyle6"
    DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/vendor/uncompyle6/uncompyle6"
    VERBATIM)
add_custom_target(evecc_python_dependencies ALL DEPENDS
    "${EVECC_LIB_STAMP}"
    "${EVECC_PYTHON_DIR}/uncompyle2.zip"
    "${EVECC_PYTHON_DIR}/uncompyle6.zip")
add_dependencies(evecc evecc_python_dependencies)

include(GNUInstallDirs)
install(TARGETS evecc RUNTIME DESTINATION ".")
install(DIRECTORY "${EVECC_PYTHON_DIR}/" DESTINATION "Python")
install(FILES readme.md DESTINATION ".")
if(EVECC_BUILD_PACKAGE)
    set(CPACK_PACKAGE_NAME "evecc")
    set(CPACK_PACKAGE_VERSION "0.1.0")
    set(CPACK_PACKAGE_FILE_NAME "evecc-${CMAKE_SYSTEM_NAME}-${CMAKE_SYSTEM_PROCESSOR}")
    set(CPACK_GENERATOR "TGZ")
    include(CPack)
endif()
