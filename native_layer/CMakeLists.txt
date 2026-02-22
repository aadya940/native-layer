cmake_minimum_required(VERSION 3.15)
project(AgentBridge)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 1. Find Python
find_package(Python3 COMPONENTS Interpreter Development REQUIRED)

# 2. Get the clean pybind11 include directory (no -I flags)
execute_process(
    COMMAND "${Python3_EXECUTABLE}" -c "import pybind11; print(pybind11.get_include())"
    OUTPUT_VARIABLE pybind11_INCLUDE_DIR
    OUTPUT_STRIP_TRAILING_WHITESPACE
)

# 3. Create the Python Module
add_library(native_plugin_layer MODULE 
    src/binding.cpp 
    src/plugin_manager.cpp
    src/watcher.cpp
)
# Strip the "lib" prefix so Python can import it directly by name
set_target_properties(native_plugin_layer PROPERTIES PREFIX "")

# 4. Include Pybind11 and Python headers
target_include_directories(native_plugin_layer PRIVATE 
    ${pybind11_INCLUDE_DIR}
    ${Python3_INCLUDE_DIRS}
)

# 5. Link Python libraries (Required for Windows MSVC)
target_link_libraries(native_plugin_layer PRIVATE 
    ${Python3_LIBRARIES}
)

# Extension suffix for macOS/Windows compatibility
if(APPLE)
  set_target_properties(native_plugin_layer PROPERTIES SUFFIX ".so")
elseif(WIN32)
  set_target_properties(native_plugin_layer PROPERTIES SUFFIX ".pyd")
endif()
