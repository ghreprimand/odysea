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
    bool stopped = false;
    {
        const std::scoped_lock guard(mutex_);
        requestedDirectory_ = directory;
        requestedToken_ = token;
        stopped = stopping_;
    }
    if (stopped) {
        // A stopped service has no worker left to establish the watch, so the
        // request would otherwise be accepted and never answered. Answering it
        // here says so: the watch will not exist, and a caller waiting to know
        // when it does is told now rather than waiting for a thread that has
        // already returned.
        if (handler_) {
            handler_(
                DirectoryWatchUpdate{.token = token,
                                     .directory = std::move(directory),
                                     .removedNames = {},
                                     .updatedEntries = {},
                                     .renamedEntries = {},
                                     .error = std::make_error_code(std::errc::operation_canceled),
                                     .rescanRequired = false,
                                     .armed = true});
        }
        return;
    }
    commandChanged_.notify_one();
    if (watcher_.has_value()) {
        watcher_->interrupt();
    }
}

void DirectoryWatchService::stop() {
    {
        const std::scoped_lock guard(mutex_);
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
            const std::scoped_lock guard(mutex_);
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
                                                  .renamedEntries = {},
                                                  .error = creationError_,
                                                  .rescanRequired = false,
                                                  .armed = true});
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
                const bool watching = watcher_->add(activeDirectory, addError);
                // Reported whether or not the watch could be established, and
                // reported after the attempt rather than before it, because
                // its whole purpose is to mark the point from which changes
                // are observed. A caller waiting for it and then reading the
                // directory leaves no interval in which a change is in
                // neither the reading nor an event. A failure is reported the
                // same way so that caller is not left waiting for a watch
                // that will never exist; it carries the error with it.
                if (handler_) {
                    handler_(DirectoryWatchUpdate{.token = activeToken,
                                                  .directory = activeDirectory,
                                                  .removedNames = {},
                                                  .updatedEntries = {},
                                                  .renamedEntries = {},
                                                  .error = watching ? std::error_code{} : addError,
                                                  .rescanRequired = false,
                                                  .armed = true});
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
            const std::scoped_lock guard(mutex_);
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
                                .renamedEntries = {},
                                .error = error,
                                .rescanRequired = false};
    std::map<std::string, PendingChange> pending;
    std::map<std::uint32_t, std::string> movedFrom;
    std::map<std::uint32_t, std::string> movedTo;

    for (const odysea::core::DirectoryChange& change : changes) {
        switch (change.kind) {
        case odysea::core::ChangeKind::Created:
        case odysea::core::ChangeKind::Modified:
        case odysea::core::ChangeKind::AttributesChanged:
        case odysea::core::ChangeKind::MovedTo:
            if (!change.name.empty()) {
                pending[change.name] = PendingChange::Update;
            }
            if (change.rename_cookie != 0 && !change.name.empty()) {
                movedTo[change.rename_cookie] = change.name;
            }
            break;
        case odysea::core::ChangeKind::Deleted:
        case odysea::core::ChangeKind::MovedFrom:
            if (!change.name.empty()) {
                pending[change.name] = PendingChange::Remove;
            }
            if (change.rename_cookie != 0 && !change.name.empty()) {
                movedFrom[change.rename_cookie] = change.name;
            }
            break;
        case odysea::core::ChangeKind::WatchRemoved:
        case odysea::core::ChangeKind::Overflow:
            update.rescanRequired = true;
            break;
        }
    }

    for (const auto& [cookie, oldName] : movedFrom) {
        const auto destination = movedTo.find(cookie);
        if (destination != movedTo.end()) {
            update.renamedEntries.push_back(
                DirectoryEntryRename{.oldName = oldName, .newName = destination->second});
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
