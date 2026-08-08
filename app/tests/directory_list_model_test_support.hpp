// Shared fixtures and probes for the directory-model suites.
//
// The model's cases are split by responsibility: how a listing is acquired
// and kept consistent, and what a person does with the rows once it is. Both
// halves build the same kinds of fixture, so the fixture builders live here
// rather than being duplicated or arbitrarily owned by one of them.
#pragma once

#include "directory_list_model.hpp"

#include "entry_launcher.hpp"
#include "file_operations_internal.hpp"

#include <QByteArray>
#include <QEventLoop>
#include <QSignalSpy>
#include <QString>
#include <QStringList>
#include <QTest>
#include <QTimer>

#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <string_view>
#include <system_error>

namespace odysea::apptest {

namespace fs = std::filesystem;

inline void writeFile(const fs::path& path, std::string_view contents = "data") {
    std::ofstream stream(path, std::ios::binary);
    stream << contents;
}

inline std::string readFile(const fs::path& path) {
    std::ifstream stream(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()};
}

inline int rowForName(const DirectoryListModel& model, const QString& name) {
    for (int row = 0; row < model.rowCount(); ++row) {
        if (model.data(model.index(row), DirectoryListModel::NameRole).toString() == name) {
            return row;
        }
    }
    return -1;
}

// A named type for the settle budget. It sits next to an entry count in the
// load measurement below, and both are integers, so transposing them would
// compile and then measure a load of a few entries against a budget of
// several thousand.
struct SettleBudget {
    qint64 milliseconds = 0;
};

inline bool waitForScan(DirectoryListModel& model) {
    if (!model.busy()) {
        return true;
    }
    QSignalSpy finished(&model, &DirectoryListModel::busyChanged);
    return finished.wait(5000) && !model.busy();
}

// Waits without the polling step a QTRY macro imposes, so a measured load
// reports the work it did rather than the next poll boundary.
inline bool waitForIdleWithin(DirectoryListModel& model, qint64 timeoutMilliseconds) {
    if (!model.busy()) {
        return true;
    }
    QEventLoop loop;
    QTimer guard;
    guard.setSingleShot(true);
    QObject::connect(&model, &DirectoryListModel::busyChanged, &loop, [&model, &loop] {
        if (!model.busy()) {
            loop.quit();
        }
    });
    QObject::connect(&guard, &QTimer::timeout, &loop, &QEventLoop::quit);
    guard.start(static_cast<int>(timeoutMilliseconds));
    loop.exec();
    return !model.busy();
}

inline bool waitForOperation(DirectoryListModel& model) {
    if (!model.operationBusy()) {
        return true;
    }
    QSignalSpy finished(&model, &DirectoryListModel::operationBusyChanged);
    return finished.wait(5000) && !model.operationBusy();
}

inline QString selectedName(const DirectoryListModel& model) {
    for (int row = 0; row < model.rowCount(); ++row) {
        if (model.data(model.index(row), DirectoryListModel::SelectedRole).toBool()) {
            return model.data(model.index(row), DirectoryListModel::NameRole).toString();
        }
    }
    return {};
}

inline QStringList selectedNames(const DirectoryListModel& model) {
    QStringList names;
    for (int row = 0; row < model.rowCount(); ++row) {
        if (model.data(model.index(row), DirectoryListModel::SelectedRole).toBool()) {
            names.push_back(model.data(model.index(row), DirectoryListModel::NameRole).toString());
        }
    }
    return names;
}

inline QString currentName(const DirectoryListModel& model) {
    const int row = model.currentIndex();
    if (row < 0 || row >= model.rowCount()) {
        return {};
    }
    return model.data(model.index(row), DirectoryListModel::NameRole).toString();
}

inline fs::path workingEntry(const fs::path& directory, odysea::core::WorkingEntryRole role) {
    std::error_code error;
    fs::directory_iterator element(directory, error);
    if (error) {
        return {};
    }
    const fs::directory_iterator end;
    for (; element != end; ++element) {
        if (odysea::core::classify_working_entry(element->path().filename().string()) == role) {
            return element->path();
        }
    }
    return {};
}

inline odysea::core::detail::RenameStep failInstallAndUnwind() {
    return [](odysea::core::detail::RenameKind kind, const fs::path& from, const fs::path& to,
              std::error_code& error) {
        if (kind == odysea::core::detail::RenameKind::Install ||
            kind == odysea::core::detail::RenameKind::Unwind) {
            error = std::make_error_code(std::errc::permission_denied);
            return;
        }
        odysea::core::detail::rename_with_filesystem(kind, from, to, error);
    };
}

class EnvironmentRestore {
  public:
    explicit EnvironmentRestore(const char* name)
        : name_(name), existed_(qEnvironmentVariableIsSet(name)), value_(qgetenv(name)) {}

    ~EnvironmentRestore() {
        if (existed_) {
            qputenv(name_, value_);
        } else {
            qunsetenv(name_);
        }
    }

    EnvironmentRestore(const EnvironmentRestore&) = delete;
    EnvironmentRestore& operator=(const EnvironmentRestore&) = delete;

  private:
    const char* name_;
    bool existed_;
    QByteArray value_;
};

class FakeEntryLauncher final : public EntryLauncher {
  public:
    bool open(const fs::path& path, std::error_code& error) override {
        ++callCount;
        openedPath = path;
        if (fail) {
            error = std::make_error_code(std::errc::permission_denied);
            return false;
        }
        error.clear();
        return true;
    }

    int callCount = 0;
    fs::path openedPath;
    bool fail = false;
};

} // namespace odysea::apptest
