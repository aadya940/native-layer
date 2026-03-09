cmake_minimum_required(VERSION 3.15)
project(native_layer)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Python COMPONENTS Interpreter Development.Module REQUIRED)
find_package(pybind11 CONFIG REQUIRED)

# Build efsw statically
set(BUILD_SHARED_LIBS OFF)
add_subdirectory(third_party/efsw)

add_library(native_plugin_layer MODULE
    native_layer/src/binding.cpp
    native_layer/src/plugin_manager.cpp
    native_layer/src/watcher.cpp
)

target_include_directories(native_plugin_layer PRIVATE
    native_layer/include
    third_party/efsw/include
)

target_link_libraries(native_plugin_layer PRIVATE
    pybind11::module
    Python::Module
    efsw
)

# Python module naming
set_target_properties(native_plugin_layer PROPERTIES
    PREFIX ""
    OUTPUT_NAME "native_plugin_layer"
)

if(WIN32)
    set_target_properties(native_plugin_layer PROPERTIES
        SUFFIX ".pyd"
    )
endif()

install(TARGETS native_plugin_layer DESTINATION .)
