#include "directory_watch_service.hpp"

#include <chrono>
#include <iterator>
#include <map>
#include <utility>

namespace {

enum class PendingChange { Remove, Update };

} // namespace

DirectoryWatchService::DirectoryWatchService(UpdateHandler handler)
    : watcher_(odysea::core::DirectoryWatcher::create(creationError_)),
      handler_(std::move(handler)), worker_([this] { run(); }) {}

DirectoryWatchService::~DirectoryWatchService() {
    stop();
}

void DirectoryWatchService::replace(std::filesystem::path directory, std::uint64_t token) {
    {
        const std::lock_guard<std::mutex> guard(mutex_);
        requestedDirectory_ = std::move(directory);
        requestedToken_ = token;
    }
    commandChanged_.notify_one();
    if (watcher_.has_value()) {
        watcher_->interrupt();
    }
}

void DirectoryWatchService::stop() {
    {
        const std::lock_guard<std::mutex> guard(mutex_);
        if (stopping_) {
            return;
        }
        stopping_ = true;
    }
    commandChanged_.notify_one();
    if (watcher_.has_value()) {
        watcher_->interrupt();
    }
    if (worker_.joinable()) {
        worker_.join();
    }
}

void DirectoryWatchService::run() {
    std::filesystem::path activeDirectory;
    std::uint64_t activeToken = 0;

    while (true) {
        std::filesystem::path requestedDirectory;
        std::uint64_t requestedToken = 0;
        {
            const std::lock_guard<std::mutex> guard(mutex_);
            if (stopping_) {
                return;
            }
            requestedDirectory = requestedDirectory_;
            requestedToken = requestedToken_;
        }

        if (!watcher_.has_value()) {
            if (requestedToken != activeToken) {
                activeToken = requestedToken;
                if (handler_) {
                    handler_(DirectoryWatchUpdate{.token = activeToken,
                                                  .directory = requestedDirectory,
                                                  .removedNames = {},
                                                  .updatedEntries = {},
                                                  .error = creationError_,
                                                  .rescanRequired = false});
                }
            }
            std::unique_lock<std::mutex> guard(mutex_);
            commandChanged_.wait(
                guard, [this, activeToken] { return stopping_ || requestedToken_ != activeToken; });
            continue;
        }

        if (requestedToken != activeToken || requestedDirectory != activeDirectory) {
            if (!activeDirectory.empty()) {
                watcher_->remove(activeDirectory);
            }
            activeDirectory = requestedDirectory;
            activeToken = requestedToken;
            if (!activeDirectory.empty()) {
                std::error_code addError;
                if (!watcher_->add(activeDirectory, addError) && handler_) {
                    handler_(DirectoryWatchUpdate{.token = activeToken,
                                                  .directory = activeDirectory,
                                                  .removedNames = {},
                                                  .updatedEntries = {},
                                                  .error = addError,
                                                  .rescanRequired = false});
                }
            }
        }

        if (activeDirectory.empty()) {
            std::unique_lock<std::mutex> guard(mutex_);
            commandChanged_.wait(
                guard, [this, activeToken] { return stopping_ || requestedToken_ != activeToken; });
            continue;
        }

        std::error_code waitError;
        std::vector<odysea::core::DirectoryChange> changes =
            watcher_->wait(odysea::core::DirectoryWatcher::wait_forever, waitError);
        if (!waitError && !changes.empty()) {
            while (true) {
                std::error_code burstError;
                auto trailingChanges = watcher_->wait(std::chrono::milliseconds{20}, burstError);
                if (burstError) {
                    waitError = burstError;
                    break;
                }
                if (trailingChanges.empty()) {
                    break;
                }

                changes.insert(changes.end(), std::make_move_iterator(trailingChanges.begin()),
                               std::make_move_iterator(trailingChanges.end()));
            }
        }

        {
            const std::lock_guard<std::mutex> guard(mutex_);
            if (stopping_) {
                return;
            }
            if (requestedToken_ != activeToken || requestedDirectory_ != activeDirectory) {
                continue;
            }
        }

        if ((!changes.empty() || waitError) && handler_) {
            handler_(makeUpdate(activeToken, activeDirectory, changes, waitError));
        }
    }
}

DirectoryWatchUpdate
DirectoryWatchService::makeUpdate(std::uint64_t token, const std::filesystem::path& directory,
                                  const std::vector<odysea::core::DirectoryChange>& changes,
                                  std::error_code error) const {
    DirectoryWatchUpdate update{.token = token,
                                .directory = directory,
                                .removedNames = {},
                                .updatedEntries = {},
                                .error = error,
                                .rescanRequired = false};
    std::map<std::string, PendingChange> pending;

    for (const odysea::core::DirectoryChange& change : changes) {
        switch (change.kind) {
        case odysea::core::ChangeKind::Created:
        case odysea::core::ChangeKind::Modified:
        case odysea::core::ChangeKind::AttributesChanged:
        case odysea::core::ChangeKind::MovedTo:
            if (!change.name.empty()) {
                pending[change.name] = PendingChange::Update;
            }
            break;
        case odysea::core::ChangeKind::Deleted:
        case odysea::core::ChangeKind::MovedFrom:
            if (!change.name.empty()) {
                pending[change.name] = PendingChange::Remove;
            }
            break;
        case odysea::core::ChangeKind::WatchRemoved:
        case odysea::core::ChangeKind::Overflow:
            update.rescanRequired = true;
            break;
        }
    }

    for (const auto& [name, action] : pending) {
        if (action == PendingChange::Remove) {
            update.removedNames.push_back(name);
            continue;
        }

        std::error_code entryError;
        const std::filesystem::directory_entry element(directory / name, entryError);
        if (!entryError) {
            update.updatedEntries.push_back(odysea::core::make_entry(element));
        } else if (entryError == std::errc::no_such_file_or_directory) {
            update.removedNames.push_back(name);
        } else {
            update.error = entryError;
            update.rescanRequired = true;
        }
    }

    return update;
}
