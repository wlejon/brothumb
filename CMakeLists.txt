cmake_minimum_required(VERSION 3.24)

project(brothumb VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(_brothumb_top_level OFF)
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    set(_brothumb_top_level ON)
endif()

option(BROTHUMB_BUILD_TESTS "Build the brothumb tests" ${_brothumb_top_level})

# Coverage. Scoped to a top-level build: a consumer that add_subdirectory()s
# brothumb has no business having its own build instrumented from in here.
option(BROTHUMB_COVERAGE "Instrument the build for gcov coverage (GCC/Clang)" OFF)
if(BROTHUMB_COVERAGE AND _brothumb_top_level)
    if(MSVC)
        message(FATAL_ERROR "BROTHUMB_COVERAGE requires GCC or Clang (gcov instrumentation); MSVC is unsupported.")
    endif()
    add_compile_options(--coverage -O0 -g)
    add_link_options(--coverage)
    message(STATUS "brothumb: coverage instrumentation ON (-O0)")
endif()

if(MSVC AND _brothumb_top_level)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

function(brothumb_warnings target)
    if(MSVC)
        target_compile_options(${target} PRIVATE /W4 /permissive- /utf-8 /MP)
        target_compile_definitions(${target} PRIVATE _CRT_SECURE_NO_WARNINGS)
    else()
        target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic)
    endif()
endfunction()

# ---- siblings --------------------------------------------------------------------
# brovfs: file types (which extractor a file goes to). broimage: decode, resize and the PNG
# (with Freedesktop text tags) the cache stores; it needs bromath (header-only).
#
# Each resolves like every bro sibling: an existing target (a superbuild already added it)
# wins; otherwise a standalone checkout beside the top-level project (../<name>, overridable
# with -D<NAME>_DIR=<path>); otherwise the top-level project's third_party/<name> submodule
# (`git submodule update --init --recursive`). third_party/ is flat and carries every
# transitive sibling: broimage's own fallback looks in the top-level third_party/ too.
#
# bromath is resolved here, before broimage, so the target exists when broimage asks for it
# and the diamond resolves once, from whichever place this project found it.
function(brothumb_sibling name what)
    if(TARGET ${name})
        return()
    endif()
    string(TOUPPER "${name}" _upper)
    set(${_upper}_DIR "${CMAKE_SOURCE_DIR}/../${name}" CACHE PATH
        "Path to standalone ${name} repo (set to a missing path to force submodule fallback)")
    if(EXISTS "${${_upper}_DIR}/CMakeLists.txt")
        message(STATUS "brothumb: ${name} from ${${_upper}_DIR}")
        add_subdirectory("${${_upper}_DIR}" "${CMAKE_BINARY_DIR}/${name}" EXCLUDE_FROM_ALL)
    elseif(EXISTS "${CMAKE_SOURCE_DIR}/third_party/${name}/CMakeLists.txt")
        message(STATUS "brothumb: ${name} from ${CMAKE_SOURCE_DIR}/third_party/${name} (submodule)")
        add_subdirectory("${CMAKE_SOURCE_DIR}/third_party/${name}" "${CMAKE_BINARY_DIR}/${name}" EXCLUDE_FROM_ALL)
    else()
        message(FATAL_ERROR
            "brothumb needs ${name} (${what}): not found at ${${_upper}_DIR} or "
            "${CMAKE_SOURCE_DIR}/third_party/${name}. Clone https://github.com/wlejon/${name} next "
            "to this repo, run `git submodule update --init --recursive`, or override with "
            "-D${_upper}_DIR=...")
    endif()
endfunction()

set(BROVFS_BUILD_TESTS OFF CACHE BOOL "" FORCE)
brothumb_sibling(brovfs "file types")

# Decode / encode / resize only: no JS API (bronze), no tensor adapter, no JIT backend.
set(BROIMAGE_ENABLE_API OFF CACHE BOOL "" FORCE)
set(BROIMAGE_WITH_TENSOR OFF CACHE BOOL "" FORCE)
set(BROIMAGE_WITH_JIT OFF CACHE BOOL "" FORCE)
set(BROIMAGE_TESTS OFF CACHE BOOL "" FORCE)
brothumb_sibling(bromath "broimage's math headers")
brothumb_sibling(broimage "decode, resize and PNG encode")

file(GLOB BROTHUMB_COMMON_SOURCES CONFIGURE_DEPENDS
    src/common/*.cpp
)

set(BROTHUMB_SOURCES ${BROTHUMB_COMMON_SOURCES})

if(WIN32)
    file(GLOB _win_sources CONFIGURE_DEPENDS src/win/*.cpp)
    list(APPEND BROTHUMB_SOURCES ${_win_sources})
elseif(APPLE)
    enable_language(OBJCXX)
    set(CMAKE_OBJCXX_STANDARD 20)
    set(CMAKE_OBJCXX_STANDARD_REQUIRED ON)
    file(GLOB _mac_sources CONFIGURE_DEPENDS src/mac/*.mm src/mac/*.cpp)
    list(APPEND BROTHUMB_SOURCES ${_mac_sources})
else()
    file(GLOB _linux_sources CONFIGURE_DEPENDS src/linux/*.cpp)
    list(APPEND BROTHUMB_SOURCES ${_linux_sources})
endif()

add_library(brothumb STATIC ${BROTHUMB_SOURCES})
add_library(brothumb::brothumb ALIAS brothumb)

target_include_directories(brothumb
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        ${CMAKE_CURRENT_SOURCE_DIR}/src/common
)

brothumb_warnings(brothumb)

find_package(Threads REQUIRED)
target_link_libraries(brothumb PUBLIC Threads::Threads brovfs::brovfs broimage::broimage)

if(WIN32)
    target_compile_definitions(brothumb PRIVATE
        NOMINMAX
        WIN32_LEAN_AND_MEAN
        UNICODE
        _UNICODE
        _WIN32_WINNT=0x0A00
    )
    target_link_libraries(brothumb PRIVATE
        shell32
        ole32
        oleaut32
        gdi32
        user32
        uuid
    )
elseif(APPLE)
    target_compile_options(brothumb PRIVATE $<$<COMPILE_LANGUAGE:OBJCXX>:-fobjc-arc -Wno-deprecated-declarations>)
    target_link_libraries(brothumb PRIVATE
        "-framework Foundation"
        "-framework CoreGraphics"
        "-framework QuickLook"
        "-framework ImageIO"
        "-framework PDFKit"
        "-framework AppKit"
    )
endif()

# --- Standalone Bronze JavaScript API ---
# On by default only when brothumb is the top-level project. A library that
# consumes brothumb then never needs bronze for it; bro forces
# BROTHUMB_ENABLE_API ON itself when it wants the binding. (The same rule is
# what keeps the nested brovfs from demanding bronze here.)
option(BROTHUMB_ENABLE_API "Build Bronze JavaScript API" ${PROJECT_IS_TOP_LEVEL})
if(BROTHUMB_ENABLE_API)
    if(NOT TARGET bronze_runtime_shared)
        set(BRONZE_BUILD_SHARED_RUNTIME ON CACHE BOOL "" FORCE)
        set(BRONZE_BUILD_TESTS OFF CACHE BOOL "" FORCE)
        if(DEFINED BRONZE_DIR AND EXISTS "${BRONZE_DIR}/CMakeLists.txt")
            # Explicit BRONZE_DIR supplied
        elseif(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../bronze/CMakeLists.txt")
            set(BRONZE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../bronze" CACHE PATH "bronze source tree")
        elseif(EXISTS "${CMAKE_SOURCE_DIR}/../bronze/CMakeLists.txt")
            set(BRONZE_DIR "${CMAKE_SOURCE_DIR}/../bronze" CACHE PATH "bronze source tree")
        endif()

        if(NOT BRONZE_DIR OR NOT EXISTS "${BRONZE_DIR}/CMakeLists.txt")
            message(FATAL_ERROR
                "bronze not found: check it out beside this repository (../bronze, with ../brass beside it) "
                "or pass -DBRONZE_DIR=<path>.")
        endif()

        add_subdirectory("${BRONZE_DIR}" "${CMAKE_BINARY_DIR}/bronze-build" EXCLUDE_FROM_ALL)
    endif()

    add_subdirectory(src/api)
endif()

if(BROTHUMB_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
