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
# (with Freedesktop text tags) the cache stores. Each resolves like bro's siblings: an existing
# target (a superbuild already added it) wins; otherwise the repo beside this one, overridable
# with -DBROVFS_DIR=<path> / -DBROIMAGE_DIR=<path>.
if(NOT TARGET brovfs)
    get_filename_component(_brovfs_default "${CMAKE_CURRENT_SOURCE_DIR}/../brovfs" ABSOLUTE)
    set(BROVFS_DIR "${_brovfs_default}" CACHE PATH "Path to the brovfs repo")
    if(NOT EXISTS "${BROVFS_DIR}/CMakeLists.txt")
        message(FATAL_ERROR "brothumb needs brovfs: clone it beside brothumb (../brovfs) or pass -DBROVFS_DIR=<path>")
    endif()
    message(STATUS "brothumb: brovfs from ${BROVFS_DIR}")
    set(BROVFS_BUILD_TESTS OFF CACHE BOOL "" FORCE)
    add_subdirectory("${BROVFS_DIR}" "${CMAKE_BINARY_DIR}/brovfs" EXCLUDE_FROM_ALL)
endif()

if(NOT TARGET broimage)
    get_filename_component(_broimage_default "${CMAKE_CURRENT_SOURCE_DIR}/../broimage" ABSOLUTE)
    set(BROIMAGE_DIR "${_broimage_default}" CACHE PATH "Path to the broimage repo")
    if(NOT EXISTS "${BROIMAGE_DIR}/CMakeLists.txt")
        message(FATAL_ERROR "brothumb needs broimage: clone it beside brothumb (../broimage, with ../bromath) or pass -DBROIMAGE_DIR=<path>")
    endif()
    message(STATUS "brothumb: broimage from ${BROIMAGE_DIR}")
    # Decode / encode / resize only: no JS API (bronze), no tensor adapter, no JIT backend.
    set(BROIMAGE_ENABLE_API OFF CACHE BOOL "" FORCE)
    set(BROIMAGE_WITH_TENSOR OFF CACHE BOOL "" FORCE)
    set(BROIMAGE_WITH_JIT OFF CACHE BOOL "" FORCE)
    set(BROIMAGE_TESTS OFF CACHE BOOL "" FORCE)
    add_subdirectory("${BROIMAGE_DIR}" "${CMAKE_BINARY_DIR}/broimage" EXCLUDE_FROM_ALL)
endif()

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

if(BROTHUMB_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
