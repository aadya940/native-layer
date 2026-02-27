cmake_minimum_required(VERSION 3.15)
project(native_layer)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Python COMPONENTS Interpreter Development.Module REQUIRED)
find_package(pybind11 CONFIG REQUIRED)

add_library(native_plugin_layer MODULE
    native_layer/src/binding.cpp
    native_layer/src/plugin_manager.cpp
    native_layer/src/watcher.cpp
)

if(MSVC)
    target_compile_options(native_plugin_layer PRIVATE /std:c++17)
endif()

target_include_directories(native_plugin_layer PRIVATE
    native_layer/include
)

target_link_libraries(native_plugin_layer PRIVATE
    pybind11::module
    Python::Module
)

set_target_properties(native_plugin_layer PROPERTIES
    PREFIX ""
    OUTPUT_NAME "native_plugin_layer"
)

install(TARGETS native_plugin_layer DESTINATION .)
