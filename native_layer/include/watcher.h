#pragma once

#include <string>
#include <functional>
#include <memory>
#include <efsw/efsw.hpp>

class DirectoryWatcher : public efsw::FileWatchListener {
public:
    DirectoryWatcher(const std::string& dir,
                     std::function<void(const std::string&, const std::string&)> callback);

    ~DirectoryWatcher();

    void start();
    void stop();

private:
    std::string directory;
    std::function<void(const std::string&, const std::string&)> on_file_ready;

    std::unique_ptr<efsw::FileWatcher> watcher;
    efsw::WatchID watch_id;

    std::string create_shadow_copy(const std::string& original_path);

    void handleFileAction(efsw::WatchID watchid,
                          const std::string& dir,
                          const std::string& filename,
                          efsw::Action action,
                          std::string oldFilename) override;
};