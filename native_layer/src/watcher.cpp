#include "../include/watcher.h"
#include <filesystem>
#include <chrono>
#include <unordered_map>
#include <iostream>

#ifdef _WIN32
    const std::string SHARED_LIB_EXT = ".dll";
#elif defined(__unix__) || defined(__APPLE__) || defined(__FreeBSD__)
    const std::string SHARED_LIB_EXT = ".so";
#else
    #error "Unsupported operating system for Native Plugin Watcher"
#endif

namespace fs = std::filesystem;

DirectoryWatcher::DirectoryWatcher(const std::string& dir, 
                                   std::function<void(const std::string&, const std::string&)> callback)
    : directory(dir), running(false), on_file_ready(callback) {}

DirectoryWatcher::~DirectoryWatcher() {
    stop();
}

void DirectoryWatcher::start() {
    running = true;
    watcher_thread = std::thread(&DirectoryWatcher::monitor_loop, this);
}

void DirectoryWatcher::stop() {
    running = false;
    if (watcher_thread.joinable()) {
        watcher_thread.join();
    }
}

// 1. The File System Isolation
std::string DirectoryWatcher::create_shadow_copy(const std::string& original_path) {
    fs::path src(original_path);
    fs::path cache_dir = src.parent_path() / ".cache";
    
    if (!fs::exists(cache_dir)) {
        fs::create_directory(cache_dir);
    }

    auto now = std::chrono::system_clock::now().time_since_epoch().count();
    fs::path shadow_path = cache_dir / (src.stem().string() + "_" + std::to_string(now) + src.extension().string());

    fs::copy_file(src, shadow_path, fs::copy_options::overwrite_existing);
    return shadow_path.string();
}

void DirectoryWatcher::monitor_loop() {
    std::unordered_map<std::string, fs::file_time_type> last_write_times;

    while (running) {
        // Ensure the directory actually exists before iterating
        if (fs::exists(directory) && fs::is_directory(directory)) {
            for (const auto& entry : fs::directory_iterator(directory)) {
                // Strictly enforce the compile-time OS extension
                if (entry.is_regular_file() && entry.path().extension() == SHARED_LIB_EXT) {
                    std::string filepath = entry.path().string();
                    // Use error_code to prevent crashes if the file is deleted or locked mid-read
                    std::error_code ec;
                    auto current_write_time = fs::last_write_time(entry, ec);
                    if (ec) continue; 

                    // Detect new or modified files
                    if (last_write_times.find(filepath) == last_write_times.end() || 
                        last_write_times[filepath] != current_write_time) {
                        try {
                            // Brief pause to allow the linker to finish its initial I/O burst
                            std::this_thread::sleep_for(std::chrono::milliseconds(100));
                            std::string shadow_path = create_shadow_copy(filepath);
                            // Extract the plugin name
                            std::string base_name = entry.path().stem().string();
                            if (base_name.length() > 3 && base_name.substr(0, 3) == "lib") {
                                base_name = base_name.substr(3);
                            }

                            // Trigger the callback to the PluginManager & Python
                            on_file_ready(base_name, shadow_path);
                            // SUCCESS: Only update the tracked timestamp if the shadow copy 
                            // and callback executed perfectly.
                            last_write_times[filepath] = current_write_time;
                        } catch (const fs::filesystem_error& e) {
                            // If cl.exe or gcc is still holding the file handle,
                            // create_shadow_copy will throw. We catch it, DO NOT update the timestamp, 
                            // and the loop will naturally try copying it again 500ms later.
                        } catch (const std::exception& e) {
                            std::cerr << "[Watcher] Fatal error processing " << filepath << ": " << e.what() << '\n';
                        }
                    }
                }
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }
}
