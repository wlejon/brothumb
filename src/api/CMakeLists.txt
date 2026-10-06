add_library(brothumb_api STATIC
    api.cpp
    host_class.cpp
    native_thumb.cpp
)

target_include_directories(brothumb_api PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/..>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/../../include>
)

target_link_libraries(brothumb_api PUBLIC
    brothumb
    bronze_runtime_shared
)

target_compile_features(brothumb_api PUBLIC cxx_std_20)

brothumb_warnings(brothumb_api)
