#include "../include/watcher.h"

#include <filesystem>
#include <chrono>
#include <iostream>

#ifdef _WIN32
const std::string SHARED_LIB_EXT = ".dll";
#elif defined(__unix__) || defined(__APPLE__) || defined(__FreeBSD__)
const std::string SHARED_LIB_EXT = ".so";
#else
#error "Unsupported operating system for Native Plugin Watcher"
#endif

namespace fs = std::filesystem;

DirectoryWatcher::DirectoryWatcher(
    const std::string& dir,
    std::function<void(const std::string&, const std::string&)> callback)
    : directory(dir), on_file_ready(callback) {}

DirectoryWatcher::~DirectoryWatcher() {
    stop();
}

void DirectoryWatcher::start() {
    watcher = std::make_unique<efsw::FileWatcher>();

    watch_id = watcher->addWatch(directory, this, false);

    watcher->watch();
}

void DirectoryWatcher::stop() {
    if (watcher) {
        watcher->removeWatch(watch_id);
        watcher.reset();
    }
}

std::string DirectoryWatcher::create_shadow_copy(const std::string& original_path) {
    fs::path src(original_path);
    fs::path cache_dir = src.parent_path() / ".cache";

    if (!fs::exists(cache_dir)) {
        fs::create_directory(cache_dir);
    }

    auto now = std::chrono::system_clock::now().time_since_epoch().count();

    fs::path shadow_path =
        cache_dir / (src.stem().string() + "_" +
                     std::to_string(now) +
                     src.extension().string());

    fs::copy_file(src, shadow_path, fs::copy_options::overwrite_existing);

    return shadow_path.string();
}

void DirectoryWatcher::handleFileAction(
    efsw::WatchID,
    const std::string& dir,
    const std::string& filename,
    efsw::Action action,
    std::string)
{
    if (action != efsw::Actions::Add &&
        action != efsw::Actions::Modified)
        return;

    fs::path path = fs::path(dir) / filename;

    if (path.extension() != SHARED_LIB_EXT)
        return;

    try {

        std::string shadow_path = create_shadow_copy(path.string());

        std::string base_name = path.stem().string();

        if (base_name.length() > 3 && base_name.substr(0,3) == "lib")
            base_name = base_name.substr(3);

        on_file_ready(base_name, shadow_path);

    }
    catch (const fs::filesystem_error&) {
        // file still being written — ignore, next event will retry
    }
    catch (const std::exception& e) {
        std::cerr << "[Watcher] Fatal error processing "
                  << path << ": " << e.what() << std::endl;
    }
}
