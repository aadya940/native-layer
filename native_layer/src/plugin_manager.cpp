#include "../include/plugin_layer.h"
#include "../include/os.h"

#include <iostream>
#include <unordered_map>
#include <shared_mutex>
#include <stdexcept>

class PluginManager {
    private:
        struct LoadedPlugin {
            void* dl_handle;
            PluginAPI* api;
        };

        std::unordered_map<std::string, LoadedPlugin> plugins;
        
        // Readers-Writer lock.
        // Multiple threads can read the plugins at the same time, but only one thread can write to the plugins.
        // This helps in hot reloading and pointer swap.
        mutable std::shared_mutex rw_lock;
        
    public:
        PluginManager() = default;
        
        ~PluginManager() {
            std::unique_lock lock(rw_lock);
            for (auto& pair: plugins){
                pair.second.api->shutdown();
                OS::close_library(pair.second.dl_handle);
            }
        }

        void load_plugin(const std::string& plugin_name, const std::string& so_path){
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
            api->initialize("{}"); // Added semicolon

            std::unique_lock lock(rw_lock);

            if (plugins.find(plugin_name) != plugins.end()) {
                plugins[plugin_name].api->shutdown();
                OS::close_library(plugins[plugin_name].dl_handle);
            }

            plugins[plugin_name] = {handle, api};
            std::cout << "Loaded native plugin: " << api->name << " v" << api->version << std::endl;
        }

        void execute(const std::string& plugin_name, 
                 const std::string& function_name, 
                 const MemoryBuffer* inputs, 
                 size_t num_inputs, 
                 MemoryBuffer* output) {
                     
            std::shared_lock lock(rw_lock);
            
            auto it = plugins.find(plugin_name);
            if (it == plugins.end()) {
                throw std::invalid_argument("Plugin not found: " + plugin_name);
            }

            int status = it->second.api->execute(function_name.c_str(), inputs, num_inputs, output);
            if (status != 0) {
                throw std::runtime_error("C++ plugin execution failed with status: " + std::to_string(status));
            }
        }

        std::string get_schema(const std::string& plugin_name) const {
            std::shared_lock lock(rw_lock);
            auto it = plugins.find(plugin_name);
            if (it == plugins.end()) {
                throw std::invalid_argument("Plugin not found: " + plugin_name);
            }
            return std::string(it->second.api->get_schema());
        }

        std::vector<std::string> get_loaded_plugins() const {
            std::shared_lock lock(rw_lock); 
            std::vector<std::string> loaded_names;
            loaded_names.reserve(plugins.size()); 
            for (const auto& pair : plugins) {
                loaded_names.push_back(pair.first);
            }
            return loaded_names;
        }
    
        void free_buffer(const std::string& plugin_name, MemoryBuffer* buffer) {
            std::shared_lock lock(rw_lock);
            auto it = plugins.find(plugin_name);
            if (it != plugins.end() && buffer->data != nullptr) {
                it->second.api->free_buffer(buffer);
            }
        }
};
