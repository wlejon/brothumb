set(COMMON_TESTS
    test_uri
    test_metadata
    test_cache
    test_generators
    test_service_async
)

foreach(test_name ${COMMON_TESTS})
    add_executable(${test_name} ${test_name}.cpp)
    target_link_libraries(${test_name} PRIVATE brothumb::brothumb)
    target_include_directories(${test_name} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    brothumb_warnings(${test_name})
    add_test(NAME ${test_name} COMMAND ${test_name})
endforeach()

if(WIN32)
    add_executable(test_win_native test_win_native.cpp)
    target_link_libraries(test_win_native PRIVATE brothumb::brothumb)
    target_include_directories(test_win_native PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    brothumb_warnings(test_win_native)
    add_test(NAME test_win_native COMMAND test_win_native)
elseif(APPLE)
    add_executable(test_mac_native test_mac_native.mm)
    target_compile_options(test_mac_native PRIVATE -fobjc-arc)
    target_link_libraries(test_mac_native PRIVATE brothumb::brothumb
        "-framework Foundation"
        "-framework QuickLook"
        "-framework CoreGraphics"
    )
    target_include_directories(test_mac_native PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    brothumb_warnings(test_mac_native)
    add_test(NAME test_mac_native COMMAND test_mac_native)
else()
    add_executable(test_linux_native test_linux_native.cpp)
    target_link_libraries(test_linux_native PRIVATE brothumb::brothumb)
    target_include_directories(test_linux_native PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    brothumb_warnings(test_linux_native)
    add_test(NAME test_linux_native COMMAND test_linux_native)
endif()

if(BROTHUMB_ENABLE_API)
    add_executable(brothumb_test_api
        test_thumb_api.cpp
    )
    target_include_directories(brothumb_test_api PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/../src/api
        ${PROJECT_SOURCE_DIR}/src
    )
    target_link_libraries(brothumb_test_api PRIVATE
        brothumb_api
        brothumb::brothumb
        bronze_runtime_shared
    )
    brothumb_warnings(brothumb_test_api)
    if(MSVC)
        target_compile_definitions(brothumb_test_api PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN UNICODE _UNICODE _WIN32_WINNT=0x0A00)
        add_custom_command(TARGET brothumb_test_api POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                $<TARGET_FILE:bronze_runtime_shared>
                $<TARGET_FILE_DIR:brothumb_test_api>
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                $<TARGET_LINKER_FILE:bronze_runtime_shared>
                $<TARGET_FILE_DIR:brothumb_test_api>
        )
    endif()

    if(NOT BRASS_HOST_BACKEND)
        message(STATUS "brothumb: brothumb_test_api not registered (brass has no code generator for this target)")
    else()
        add_test(NAME brothumb_test_api COMMAND brothumb_test_api)
        set_tests_properties(brothumb_test_api PROPERTIES TIMEOUT 120 SKIP_RETURN_CODE 77)

        add_test(NAME brothumb_test_api_gc_stress COMMAND brothumb_test_api)
        set_tests_properties(brothumb_test_api_gc_stress PROPERTIES
            TIMEOUT 120
            SKIP_RETURN_CODE 77
            ENVIRONMENT "BRONZE_GC_STRESS=1;BRONZE_GC_POISON=1"
        )
        if(WIN32)
            set_tests_properties(brothumb_test_api PROPERTIES
                ENVIRONMENT "PATH=$<TARGET_FILE_DIR:bronze_runtime_shared>;$<TARGET_FILE_DIR:brothumb_test_api>;$ENV{PATH}")
            set_property(TEST brothumb_test_api_gc_stress APPEND PROPERTY
                ENVIRONMENT "PATH=$<TARGET_FILE_DIR:bronze_runtime_shared>;$<TARGET_FILE_DIR:brothumb_test_api>;$ENV{PATH}")
        endif()
    endif()
endif()

# check.h's skip() exits 77 when a tool or platform feature is absent: ctest reports that as
# a skip, not a failure.
get_property(_brothumb_tests DIRECTORY PROPERTY TESTS)
set_tests_properties(${_brothumb_tests} PROPERTIES SKIP_RETURN_CODE 77)

# Every test executable links no_dialogs.cpp (see the file).
if(WIN32)
    get_property(_test_targets DIRECTORY PROPERTY BUILDSYSTEM_TARGETS)
    foreach(_t IN LISTS _test_targets)
        get_target_property(_type ${_t} TYPE)
        if(_type STREQUAL "EXECUTABLE")
            target_sources(${_t} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/no_dialogs.cpp)
        endif()
    endforeach()
endif()
