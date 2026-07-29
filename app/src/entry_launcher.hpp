// Injectable application-layer seam for opening local files.
#pragma once

#include <filesystem>
#include <system_error>

class EntryLauncher {
  public:
    virtual ~EntryLauncher() = default;

    [[nodiscard]] virtual bool open(const std::filesystem::path& path, std::error_code& error) = 0;
};

class DesktopEntryLauncher final : public EntryLauncher {
  public:
    [[nodiscard]] bool open(const std::filesystem::path& path, std::error_code& error) override;
};
