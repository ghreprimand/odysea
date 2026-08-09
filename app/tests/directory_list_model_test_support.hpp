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

#include <dlfcn.h>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <string_view>
#include <system_error>

namespace odysea::apptest {

// Whether this binary is running under AddressSanitizer.
//
// Detected through the sanitizer runtime's own entry point rather than a
// preprocessor macro. The macro form worked, but it was a free-standing block
// of `#ifdef` lines between two case definitions, and splitting this file
// along function boundaries dropped it without a word: the build stopped
// defining the symbol, the instrumented branch went dead, and every case that
// selected a budget silently took the release one. Nothing failed, because a
// budget that is too tight for an instrumented build is still satisfied on a
// fast machine most of the time.
//
// Ordinary code cannot be lost that way. The runtime's entry point is looked
// up by name at run time rather than declared, because declaring it would mean
// spelling a reserved identifier in tracked source; a string passed to the
// dynamic loader carries no such claim.
inline bool runningUnderAddressSanitizer() noexcept {
    static const bool instrumented = ::dlsym(RTLD_DEFAULT, "__asan_init") != nullptr;
    return instrumented;
}

// The wall-clock budget for a timed case, in milliseconds. An instrumented
// build carries roughly twenty times the release cost, so it gets its own
// figure rather than one bound loose enough to pass there and useless here.
inline qint64 timingBudget(qint64 releaseMilliseconds, qint64 instrumentedMilliseconds) noexcept {
    return runningUnderAddressSanitizer() ? instrumentedMilliseconds : releaseMilliseconds;
}

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

// Probes that read the model's derived state directly. They live here, and
// are befriended once, because every suite over this model needs them and a
// probe duplicated per suite is a probe that drifts between them.
struct ModelProbe {
    // Empty when the row keys and the key index agree with the entries they
    // are derived from; otherwise the first disagreement, so a failure names
    // the row rather than only the count.
    static QString rowKeyIndexMismatch(const DirectoryListModel& model);
    // How many contiguous runs of rows the given needle would remove from the
    // rows currently presented. A scattered removal's cost depends on the run
    // count, not the removed-row count, so a case that measures one has to
    // establish that its filter really is scattered.
    static int removalRunCount(const DirectoryListModel& model, const QString& needle);
    // Empty when the name index over the scanned listing holds the first row
    // of each name, and when every scanned entry's key is its name under the
    // scanned directory — the equality the one index rests on.
    static QString scannedNameIndexMismatch(const DirectoryListModel& model);
};

inline QString ModelProbe::rowKeyIndexMismatch(const DirectoryListModel& model) {
    if (model.entryKeys_.size() != model.entries_.size()) {
        return QStringLiteral("key count %1 does not match row count %2")
            .arg(model.entryKeys_.size())
            .arg(model.entries_.size());
    }

    QHash<QString, int> expectedRows;
    for (std::size_t row = 0; row < model.entryKeys_.size(); ++row) {
        const QString expected = model.entryKey(model.entries_.at(row));
        if (model.entryKeys_.at(row) != expected) {
            return QStringLiteral("row %1 key %2 does not match entry key %3")
                .arg(row)
                .arg(model.entryKeys_.at(row), expected);
        }
        if (!expectedRows.contains(expected)) {
            expectedRows.insert(expected, static_cast<int>(row));
        }
    }
    if (model.entryRowsByKey_ != expectedRows) {
        return QStringLiteral("key index does not hold the first row of each key");
    }
    for (auto element = expectedRows.constBegin(); element != expectedRows.constEnd(); ++element) {
        if (model.rowForEntryKey(element.key()) != element.value()) {
            return QStringLiteral("lookup of %1 did not return its first row %2")
                .arg(element.key())
                .arg(element.value());
        }
    }
    if (model.rowForEntryKey(QStringLiteral("/absent")) != -1 ||
        model.rowForEntryKey(QString{}) != -1) {
        return QStringLiteral("lookup of an absent key did not report no row");
    }
    return {};
}
inline int ModelProbe::removalRunCount(const DirectoryListModel& model, const QString& needle) {
    int runs = 0;
    bool departing = false;
    for (const odysea::core::Entry& entry : model.entries_) {
        const bool survives =
            QString::fromStdString(entry.name).contains(needle, Qt::CaseInsensitive);
        if (!survives && !departing) {
            ++runs;
        }
        departing = !survives;
    }
    return runs;
}
inline QString ModelProbe::scannedNameIndexMismatch(const DirectoryListModel& model) {
    QHash<QString, std::size_t> expected;
    expected.reserve(static_cast<qsizetype>(model.scannedEntries_.size()));
    for (std::size_t row = 0; row < model.scannedEntries_.size(); ++row) {
        const odysea::core::Entry& entry = model.scannedEntries_.at(row);
        const QString name = QString::fromStdString(entry.name);
        // The rule the index rests on. Every entry in a listing has the
        // scanned directory as its parent, so its key is its name under that
        // directory. Matching on the key and matching on the name therefore
        // cannot select different entries, which is what makes one name index
        // able to answer both.
        const QString keyUnderScannedDirectory = QString::fromStdString(
            (fs::path(model.scannedPath_.toStdString()) / entry.name).lexically_normal().string());
        if (model.entryKey(entry) != keyUnderScannedDirectory) {
            return QStringLiteral("scanned entry %1 has key %2 rather than %3")
                .arg(name, model.entryKey(entry), keyUnderScannedDirectory);
        }
        if (!expected.contains(name)) {
            expected.insert(name, row);
        }
    }
    if (expected.size() != static_cast<qsizetype>(model.scannedEntries_.size())) {
        return QStringLiteral("scanned listing holds %1 entries under %2 names")
            .arg(model.scannedEntries_.size())
            .arg(expected.size());
    }
    if (model.scannedRowsByName_ != expected) {
        return QStringLiteral("name index does not hold the first row of each scanned name");
    }
    return {};
}
} // namespace odysea::apptest
