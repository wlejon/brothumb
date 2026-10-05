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

# check.h's skip() exits 77 when a tool or platform feature is absent: ctest reports that as
# a skip, not a failure.
get_property(_brothumb_tests DIRECTORY PROPERTY TESTS)
set_tests_properties(${_brothumb_tests} PROPERTIES SKIP_RETURN_CODE 77)
