#pragma once

#include "odysea/core/directory_model.hpp"
#include "odysea/core/directory_watcher.hpp"

#include <condition_variable>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <system_error>
#include <thread>
#include <vector>

struct DirectoryWatchUpdate {
    std::uint64_t token = 0;
    std::filesystem::path directory;
    std::vector<std::string> removedNames;
    std::vector<odysea::core::Entry> updatedEntries;
    std::error_code error;
    bool rescanRequired = false;
};

class DirectoryWatchService {
  public:
    using UpdateHandler = std::function<void(DirectoryWatchUpdate update)>;

    explicit DirectoryWatchService(UpdateHandler handler);
    ~DirectoryWatchService();

    DirectoryWatchService(const DirectoryWatchService&) = delete;
    DirectoryWatchService& operator=(const DirectoryWatchService&) = delete;
    DirectoryWatchService(DirectoryWatchService&&) = delete;
    DirectoryWatchService& operator=(DirectoryWatchService&&) = delete;

    void replace(std::filesystem::path directory, std::uint64_t token);
    void stop();

  private:
    void run();
    [[nodiscard]] DirectoryWatchUpdate
    makeUpdate(std::uint64_t token, const std::filesystem::path& directory,
               const std::vector<odysea::core::DirectoryChange>& changes,
               std::error_code error) const;

    std::optional<odysea::core::DirectoryWatcher> watcher_;
    std::error_code creationError_;
    UpdateHandler handler_;
    std::mutex mutex_;
    std::condition_variable commandChanged_;
    std::filesystem::path requestedDirectory_;
    std::uint64_t requestedToken_ = 0;
    bool stopping_ = false;
    std::thread worker_;
};
