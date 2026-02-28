#include "../include/plugin_manager.h"
#include "../include/os.h"

#include <iostream>
#include <mutex>
#include <stdexcept>
#include <type_traits>

namespace {
    template<typename Func>
    auto safe_execute(const std::string& plugin_name, const std::string& operation, Func&& func) {
        try {
            return func();
        } catch (const std::exception& e) {
            std::cerr << "C++ CRASH " << operation << " for plugin '" << plugin_name << "': " << e.what() << std::endl;
            if constexpr (std::is_void_v<decltype(func())>) {
                return;
            } else {
                // Return a special error value that indicates a C++ crash
                if constexpr (std::is_same_v<decltype(func()), int>) {
                    return -999; // Special error code for C++ crashes
                } else {
                    return decltype(func()){};
                }
            }
        } catch (...) {
            std::cerr << "C++ CRASH " << operation << " for plugin '" << plugin_name << "'" << std::endl;
            if constexpr (std::is_void_v<decltype(func())>) {
                return;
            } else {
                // Return a special error value that indicates a C++ crash
                if constexpr (std::is_same_v<decltype(func()), int>) {
                    return -999; // Special error code for C++ crashes
                } else {
                    return decltype(func()){};
                }
            }
        }
    }
    
    void reset_output_buffer(MemoryBuffer* output) {
        output->data = nullptr;
        output->size = 0;
        output->type_id = TYPE_UNKNOWN;
        output->dtype = DTYPE_F64;
    }
}

PluginManager::~PluginManager() {
    std::unique_lock lock(rw_lock);
    for (auto& pair: plugins){
        if (pair.second.api && pair.second.api->shutdown) {
            pair.second.api->shutdown();
        }
        OS::close_library(pair.second.dl_handle);
    }
}

void PluginManager::load_plugin(const std::string& plugin_name, const std::string& so_path){
    void* handle = OS::load_library(so_path);
    if (!handle) {
        throw std::runtime_error("Library load failed for " + so_path + ": " + OS::get_last_error());
    }

    GetPluginAPIFunc get_api = (GetPluginAPIFunc) OS::get_symbol(handle, "get_plugin_api");
    if (!get_api) {
        OS::close_library(handle);
        throw std::runtime_error("Symbol lookup failed. Is get_plugin_api exported? Error: " + OS::get_last_error());
    }

    PluginAPI* api = get_api();
    if (api->initialize) {
        api->initialize("{}");
    }

    std::unique_lock lock(rw_lock);

    if (plugins.find(plugin_name) != plugins.end()) {
        if (plugins[plugin_name].api->shutdown) {
            plugins[plugin_name].api->shutdown();
        }
        OS::close_library(plugins[plugin_name].dl_handle);
    }

    plugins[plugin_name] = {handle, api};
    std::cout << "Loaded native plugin: " << api->name << " v" << api->version << std::endl;
}

void PluginManager::execute(const std::string& plugin_name, 
                 const std::string& function_name, 
                 const MemoryBuffer* inputs, 
                 size_t num_inputs, 
                 MemoryBuffer* output) {
                      
    std::shared_lock lock(rw_lock);
    
    auto it = plugins.find(plugin_name);
    if (it == plugins.end()) {
        throw std::invalid_argument("Plugin not found: " + plugin_name);
    }

    int status = safe_execute(plugin_name, "executing function '" + function_name + "'", [&]() {
        return it->second.api->execute(function_name.c_str(), inputs, num_inputs, output);
    });
    
    if (status == -999) {
        // C++ crash occurred - return safe result instead of crashing Python
        reset_output_buffer(output);
        return;
    }
    
    if (status != 0) {
        throw std::runtime_error("C++ plugin execution failed with status " + std::to_string(status) +
                                 " for plugin '" + plugin_name + "' function '" + function_name + "'");
    }
}

std::string PluginManager::get_schema(const std::string& plugin_name) const {
    std::shared_lock lock(rw_lock);
    auto it = plugins.find(plugin_name);
    if (it == plugins.end()) {
        std::cerr << "Plugin not found for schema: " << plugin_name << std::endl;
        return "{}";
    }
    
    return safe_execute(plugin_name, "getting schema", [&]() {
        return std::string(it->second.api->get_schema());
    });
}

std::vector<std::string> PluginManager::get_loaded_plugins() const {
    std::shared_lock lock(rw_lock); 
    std::vector<std::string> loaded_names;
    loaded_names.reserve(plugins.size()); 
    for (const auto& pair : plugins) {
        loaded_names.push_back(pair.first);
    }
    return loaded_names;
}

void PluginManager::free_buffer(const std::string& plugin_name, MemoryBuffer* buffer) {
    std::shared_lock lock(rw_lock);
    auto it = plugins.find(plugin_name);
    if (it != plugins.end() && buffer->data != nullptr) {
        safe_execute(plugin_name, "freeing buffer", [&]() {
            it->second.api->free_buffer(buffer);
        });
    }
}
