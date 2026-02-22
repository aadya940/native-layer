/* Watches the filesystem for changes in the plugin directory and reloads plugins automatically. */
#pragma once

#include <string>
#include <functional>
#include <atomic>
#include <thread>

class DirectoryWatcher {
private:
    std::string directory;
    std::atomic<bool> running;
    std::thread watcher_thread;
    
    // The callback fired when a shadow copy is ready
    std::function<void(const std::string& plugin_name, const std::string& shadow_path)> on_file_ready;

    void monitor_loop();
    std::string create_shadow_copy(const std::string& original_path);

public:
    DirectoryWatcher(const std::string& dir, 
                     std::function<void(const std::string&, const std::string&)> callback);
    ~DirectoryWatcher();

    void start();
    void stop();
};

