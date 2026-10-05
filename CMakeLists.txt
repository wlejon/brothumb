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

file(GLOB BROTHUMB_COMMON_SOURCES CONFIGURE_DEPENDS
    src/common/*.cpp
    src/third_party/lodepng/*.cpp
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
        ${CMAKE_CURRENT_SOURCE_DIR}/src/third_party
)

brothumb_warnings(brothumb)

find_package(Threads REQUIRED)
target_link_libraries(brothumb PUBLIC Threads::Threads)

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

if(MSVC)
    set_source_files_properties(
        src/third_party/lodepng/lodepng.cpp
        PROPERTIES COMPILE_FLAGS "/wd4267 /wd4334"
    )
else()
    # lodepng uses standard C/C++ idioms that pedantic flags on GCC/Clang
    set_source_files_properties(
        src/third_party/lodepng/lodepng.cpp
        PROPERTIES COMPILE_OPTIONS "-Wno-unused-function;-Wno-unused-parameter"
    )
endif()

if(BROTHUMB_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
