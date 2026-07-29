#include "entry_launcher.hpp"

#include <QDesktopServices>
#include <QUrl>

bool DesktopEntryLauncher::open(const std::filesystem::path& path, std::error_code& error) {
    error.clear();
    if (QDesktopServices::openUrl(QUrl::fromLocalFile(QString::fromStdString(path.string())))) {
        return true;
    }
    error = std::make_error_code(std::errc::io_error);
    return false;
}
