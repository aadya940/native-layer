#pragma once
#include <string>
#include <vector>
#include <unordered_map>
#include <shared_mutex>
#include "plugin_layer.h"

class PluginManager {
private:
    struct LoadedPlugin {
        void* dl_handle;
        PluginAPI* api;
    };

    std::unordered_map<std::string, LoadedPlugin> plugins;
    
    mutable std::shared_mutex rw_lock;

public:
    PluginManager() = default;
    ~PluginManager();

    void load_plugin(const std::string& plugin_name, const std::string& so_path);
    
    void execute(const std::string& plugin_name, 
                 const std::string& function_name, 
                 const MemoryBuffer* inputs, 
                 size_t num_inputs, 
                 MemoryBuffer* output);

    std::string get_schema(const std::string& plugin_name) const;
    
    std::vector<std::string> get_loaded_plugins() const;
    
    void free_buffer(const std::string& plugin_name, MemoryBuffer* buffer);
};
