#include "directory_list_model.hpp"

#include "entry_launcher.hpp"
#include "file_operations_internal.hpp"

#include <QByteArray>
#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QPersistentModelIndex>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QThreadPool>
#include <QTimer>
#include <QUrl>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <string>

namespace fs = std::filesystem;

namespace {

void writeFile(const fs::path& path, std::string_view contents = "data") {
    std::ofstream stream(path, std::ios::binary);
    stream << contents;
}

std::string readFile(const fs::path& path) {
    std::ifstream stream(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()};
}

int rowForName(const DirectoryListModel& model, const QString& name) {
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

bool waitForScan(DirectoryListModel& model) {
    if (!model.busy()) {
        return true;
    }
    QSignalSpy finished(&model, &DirectoryListModel::busyChanged);
    return finished.wait(5000) && !model.busy();
}

// Waits without the polling step a QTRY macro imposes, so a measured load
// reports the work it did rather than the next poll boundary.
bool waitForIdleWithin(DirectoryListModel& model, qint64 timeoutMilliseconds) {
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

bool waitForOperation(DirectoryListModel& model) {
    if (!model.operationBusy()) {
        return true;
    }
    QSignalSpy finished(&model, &DirectoryListModel::operationBusyChanged);
    return finished.wait(5000) && !model.operationBusy();
}

QString selectedName(const DirectoryListModel& model) {
    for (int row = 0; row < model.rowCount(); ++row) {
        if (model.data(model.index(row), DirectoryListModel::SelectedRole).toBool()) {
            return model.data(model.index(row), DirectoryListModel::NameRole).toString();
        }
    }
    return {};
}

QStringList selectedNames(const DirectoryListModel& model) {
    QStringList names;
    for (int row = 0; row < model.rowCount(); ++row) {
        if (model.data(model.index(row), DirectoryListModel::SelectedRole).toBool()) {
            names.push_back(model.data(model.index(row), DirectoryListModel::NameRole).toString());
        }
    }
    return names;
}

QString currentName(const DirectoryListModel& model) {
    const int row = model.currentIndex();
    if (row < 0 || row >= model.rowCount()) {
        return {};
    }
    return model.data(model.index(row), DirectoryListModel::NameRole).toString();
}

fs::path workingEntry(const fs::path& directory, odysea::core::WorkingEntryRole role) {
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

odysea::core::detail::RenameStep failInstallAndUnwind() {
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

} // namespace

class DirectoryListModelTest : public QObject {
    Q_OBJECT

    // Empty when the row keys and the key index agree with the entries they
    // are derived from; otherwise the first disagreement, so a failure names
    // the row rather than only the count.
    struct LoadMeasurement {
        qint64 firstScanMilliseconds = 0;
        qint64 refreshMilliseconds = 0;
        quint64 keyBuilds = 0;
    };

    static QString rowKeyIndexMismatch(const DirectoryListModel& model);
    static QString measureDirectoryLoad(int entryCount, SettleBudget settleBudget,
                                        LoadMeasurement& measurement);

  private slots:
    void rapidNavigationCancelsStaleBatches();
    void incrementalScannerPublishesBatches();
    void watchAndPresentationUpdatesUseGranularSignals();
    void navigationHistorySurvivesIncrementalUpdates();
    void watcherBurstPreservesRenamedSelection();
    void hardLinksRemainDistinctAcrossRefreshAndRename();
    void uniqueInodeFallbackPreservesRename();
    void recycledSubvolumeIdentityDoesNotMoveSelection();
    void ambiguousIdentityDoesNotMoveSelection();
    void recycledIdentityDoesNotMoveSelectionAcrossRefresh();
    void selectionSurvivesSortFilterAndRefresh();
    void explicitGeometricSelectionUsesRowSet();
    void cursorMovementAndPrefixSearchPreserveSelectionContracts();
    void sameParentCopyCreatesSiblingDuplicate();
    void directDirectoryActivationNavigates();
    void navigationInputResolvesTilde();
    void directPathInputValidatesDirectory();
    void invalidDirectPathInputDoesNotNavigate();
    void sharedPathCompletionFinishesTheNextSegmentPrefix();
    void uniquePathCompletionFinishesTheDirectoryName();
    void activationBreadcrumbsAndDropContracts();
    void symlinkTargetDirectoryChangesRefreshRole();
    void operationsReachCoreAndReportFailures();
    void retainedRecoveryRemainsVisibleDuringOperation();
    void overflowRequestsARescan();
    void destructionDropsQueuedWorkerCallbacks();
    void largeDirectoryLoadStaysWithinBudget();
    void heldBackScanEntriesReachTheCompletedListing();
    void republishedEntriesUpdateOneRowRatherThanAddingAnother();
    void supersededScanDropsTheEntriesItHeldBack();
    void rowKeyIndexTracksEveryRowMutation();
    void duplicateResolvedKeysCompareAgainstTheFirstRow();
};

QString DirectoryListModelTest::rowKeyIndexMismatch(const DirectoryListModel& model) {
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

// The sanitizer build carries roughly an order of magnitude of instrumentation
// overhead, so it gets its own budget rather than a bound loose enough to pass
// there and useless in the release build.
#ifdef __SANITIZE_ADDRESS__
#define ODYSEA_INSTRUMENTED_BUILD 1
#endif
#ifdef __has_feature
#if __has_feature(address_sanitizer)
#define ODYSEA_INSTRUMENTED_BUILD 1
#endif
#endif

QString DirectoryListModelTest::measureDirectoryLoad(int entryCount, SettleBudget settleBudget,
                                                     LoadMeasurement& measurement) {
    QTemporaryDir fixture;
    if (!fixture.isValid()) {
        return QStringLiteral("temporary directory unavailable");
    }
    const fs::path root = fixture.path().toStdString();
    for (int index = 0; index < entryCount; ++index) {
        writeFile(root / ("entry-" + std::to_string(index)));
    }

    DirectoryListModel model;
    QElapsedTimer timer;
    timer.start();
    model.setPath(fixture.path());
    if (!waitForIdleWithin(model, settleBudget.milliseconds)) {
        return QStringLiteral("first scan of %1 entries did not settle").arg(entryCount);
    }
    measurement.firstScanMilliseconds = timer.elapsed();
    if (model.rowCount() != entryCount) {
        return QStringLiteral("first scan presented %1 of %2 entries")
            .arg(model.rowCount())
            .arg(entryCount);
    }

    timer.restart();
    model.refresh();
    if (!waitForIdleWithin(model, settleBudget.milliseconds)) {
        return QStringLiteral("refresh of %1 entries did not settle").arg(entryCount);
    }
    measurement.refreshMilliseconds = timer.elapsed();
    if (model.rowCount() != entryCount) {
        return QStringLiteral("refresh presented %1 of %2 entries")
            .arg(model.rowCount())
            .arg(entryCount);
    }

    QString mismatch = rowKeyIndexMismatch(model);
    if (!mismatch.isEmpty()) {
        return mismatch;
    }
    measurement.keyBuilds = model.entryKeyBuilds_;
    return {};
}

void DirectoryListModelTest::largeDirectoryLoadStaysWithinBudget() {
    // Directories large enough for reconciliation to dominate the fixed cost
    // of starting a scan. Every other case in this file uses a handful of
    // entries, which is why a load that grew with the square of the directory
    // size went unnoticed.
    //
    // Three bounds, because no one of them holds the shape on its own.
    //
    // The count of key constructions carries the algorithmic shape. It is
    // machine-independent, and each construction normalizes a path and
    // allocates a string, which is what the cost is made of. Measured over a
    // load and a refresh it is 25.4 per entry at 4,000 and 25.9 at 8,000, and
    // it stays within a percent of 26 out to 128,000. That flat rate is the
    // property worth holding, so it is bounded directly.
    //
    // The ratio between the two sizes bounds the exponent rather than the
    // rate. A growth ratio was useless while both the healthy and the
    // defective states were quadratic, because every reading landed near four.
    // It discriminates now precisely because publishing on a growing interval
    // moved the healthy state to linear: doubling the entry count doubles the
    // count here and quadruples it if the interval stops growing. Measured
    // 2.04 healthy against 3.74 for a fixed publishing interval, 3.66 for a
    // linear rescan per delivered entry.
    //
    // Wall clock catches the catastrophic case and nothing finer: it moves
    // with machine load, and the sanitizer build carries roughly twenty times
    // the release cost, so any bound loose enough to be safe there admits a
    // real regression here.
    constexpr quint64 keyBuildsPerEntryCeiling = 60;
    constexpr double keyBuildGrowthCeiling = 2.8;
#ifdef ODYSEA_INSTRUMENTED_BUILD
    constexpr qint64 budgetMilliseconds = 20000;
#else
    constexpr qint64 budgetMilliseconds = 3000;
#endif
    constexpr int smallerEntryCount = 4000;
    constexpr int largerEntryCount = 2 * smallerEntryCount;

    LoadMeasurement smaller;
    LoadMeasurement larger;
    const SettleBudget settleBudget{.milliseconds = 4 * budgetMilliseconds};
    QCOMPARE(measureDirectoryLoad(smallerEntryCount, settleBudget, smaller), QString{});
    QCOMPARE(measureDirectoryLoad(largerEntryCount, settleBudget, larger), QString{});

    const double growth = static_cast<double>(larger.keyBuilds) /
                          static_cast<double>(std::max<quint64>(smaller.keyBuilds, 1));
    const quint64 keyBuildCeiling =
        keyBuildsPerEntryCeiling * static_cast<quint64>(largerEntryCount);

    // Reported unconditionally so a failure arrives with the measurements that
    // produced it instead of only the bounds it missed.
    qInfo("%d entries: scan %lld ms, refresh %lld ms, %llu keys built", smallerEntryCount,
          static_cast<long long>(smaller.firstScanMilliseconds),
          static_cast<long long>(smaller.refreshMilliseconds),
          static_cast<unsigned long long>(smaller.keyBuilds));
    qInfo("%d entries: scan %lld ms, refresh %lld ms, %llu keys built, ceiling %llu",
          largerEntryCount, static_cast<long long>(larger.firstScanMilliseconds),
          static_cast<long long>(larger.refreshMilliseconds),
          static_cast<unsigned long long>(larger.keyBuilds),
          static_cast<unsigned long long>(keyBuildCeiling));
    qInfo("key construction growth %.2f across a doubled directory, ceiling %.2f, budget %lld ms",
          growth, keyBuildGrowthCeiling, static_cast<long long>(budgetMilliseconds));

    QVERIFY(larger.keyBuilds < keyBuildCeiling);
    QVERIFY(growth < keyBuildGrowthCeiling);
    QVERIFY(smaller.firstScanMilliseconds < budgetMilliseconds);
    QVERIFY(smaller.refreshMilliseconds < budgetMilliseconds);
    QVERIFY(larger.firstScanMilliseconds < budgetMilliseconds);
    QVERIFY(larger.refreshMilliseconds < budgetMilliseconds);
}

void DirectoryListModelTest::heldBackScanEntriesReachTheCompletedListing() {
    // A scan publishes on a growing interval, so the entries delivered after
    // its last publication are still held when the scan completes. Completion
    // replaces the scanned listing outright, so those entries are lost rather
    // than merely late if the completion path does not take them first.
    //
    // The count is chosen to leave a remainder: it is one publishing interval
    // plus a partial one, so entries are certain to be outstanding at
    // completion.
    constexpr int entryCount = 200;

    QTemporaryDir fixture;
    QVERIFY(fixture.isValid());
    const fs::path root = fixture.path().toStdString();
    for (int index = 0; index < entryCount; ++index) {
        writeFile(root / ("entry-" + std::to_string(index)));
    }

    DirectoryListModel model;

    // A held-back group is published as one insertion covering more rows than
    // a delivered batch, and a view answers that signal by asking the model
    // for data. So the row keys and the key index are checked from inside the
    // signal as well as after it.
    QString liveMismatch;
    int liveChecks = 0;
    const auto observe = [&model, &liveMismatch, &liveChecks] {
        ++liveChecks;
        if (liveMismatch.isEmpty()) {
            liveMismatch = rowKeyIndexMismatch(model);
        }
    };
    connect(&model, &QAbstractItemModel::rowsInserted, &model, observe);
    connect(&model, &QAbstractItemModel::rowsRemoved, &model, observe);

    model.setPath(fixture.path());
    QVERIFY(waitForIdleWithin(model, 10000));

    QCOMPARE(model.rowCount(), entryCount);
    QCOMPARE(model.scannedEntries_.size(), static_cast<std::size_t>(entryCount));
    QVERIFY(model.pendingScanEntries_.empty());
    // Named, not just counted: a listing of the right size assembled from the
    // wrong entries would pass a count on its own.
    for (int index = 0; index < entryCount; ++index) {
        QVERIFY2(rowForName(model, QStringLiteral("entry-%1").arg(index)) >= 0,
                 qPrintable(QStringLiteral("entry-%1 is missing").arg(index)));
    }

    // Asserted, not assumed: a silent observer that never ran would report a
    // clean tree it had not looked at.
    QVERIFY(liveChecks > 0);
    QCOMPARE(liveMismatch, QString{});
}

void DirectoryListModelTest::republishedEntriesUpdateOneRowRatherThanAddingAnother() {
    // A publication merges the entries it holds into the scanned listing by
    // key. A key it already carries has to update that entry in place: a
    // refresh redelivers the whole directory, so appending instead would
    // present every entry a second time for as long as the refresh ran, and
    // the duplicates would disappear only when completion replaced the
    // listing outright. That makes the damage invisible to anything that
    // inspects a settled model.
    //
    // The delivery is driven directly, at one publishing interval, because a
    // real refresh decides which entries land in which delivery by readdir
    // order and would leave the case to timing.
    constexpr int entryCount = 128;

    QTemporaryDir fixture;
    QVERIFY(fixture.isValid());
    const fs::path root = fixture.path().toStdString();
    const fs::path nested = root / "listing";
    fs::create_directories(nested);
    for (int index = 0; index < entryCount; ++index) {
        writeFile(nested / ("entry-" + std::to_string(index)), "a");
    }

    DirectoryListModel model;
    model.setPath(QString::fromStdString(nested.string()));
    QVERIFY(waitForIdleWithin(model, 5000));
    QCOMPARE(model.rowCount(), entryCount);
    model.watchService_.stop();

    std::vector<odysea::core::Entry> delivery;
    delivery.reserve(static_cast<std::size_t>(entryCount) + 1);
    for (int index = 0; index < entryCount; ++index) {
        delivery.push_back(odysea::core::make_entry(
            fs::directory_entry(nested / ("entry-" + std::to_string(index)))));
    }
    // The repeat carries later metadata than the entry already presented, so
    // the case can tell an update apart from a delivery that was dropped.
    const fs::path repeated = nested / "entry-0";
    writeFile(repeated, "grown past its first size");
    const auto expectedSize = static_cast<qulonglong>(fs::file_size(repeated));
    delivery.push_back(odysea::core::make_entry(fs::directory_entry(repeated)));

    model.receiveScanBatch(model.activeScanToken_, delivery);

    QCOMPARE(model.pendingScanEntries_.size(), std::size_t{0});
    QCOMPARE(model.scannedEntries_.size(), static_cast<std::size_t>(entryCount));
    QCOMPARE(model.rowCount(), entryCount);
    const int repeatedRow = rowForName(model, QStringLiteral("entry-0"));
    QVERIFY(repeatedRow >= 0);
    QCOMPARE(model.data(model.index(repeatedRow), DirectoryListModel::SizeRole).toULongLong(),
             expectedSize);
}

void DirectoryListModelTest::supersededScanDropsTheEntriesItHeldBack() {
    // Entries held back belong to the scan that delivered them. A scan that
    // supersedes it presents a different directory, and merging the held-back
    // entries into that listing would show a departed directory's contents
    // under the current path.
    //
    // The delivery is driven directly rather than raced: entries have to be
    // outstanding at the moment the next scan starts, and waiting for a real
    // scan to reach that state would make the case decide by timing.
    QTemporaryDir fixture;
    QVERIFY(fixture.isValid());
    const fs::path root = fixture.path().toStdString();
    const fs::path abandoned = root / "abandoned";
    const fs::path destination = root / "destination";
    fs::create_directories(abandoned);
    fs::create_directories(destination);
    writeFile(abandoned / "stale.txt");
    writeFile(destination / "winner.txt");

    DirectoryListModel model;
    model.setPath(QString::fromStdString(abandoned.string()));
    QVERIFY(waitForIdleWithin(model, 5000));

    // Below the publishing interval, so the model holds this rather than
    // presenting it.
    std::vector<odysea::core::Entry> held;
    held.push_back(odysea::core::make_entry(fs::directory_entry(abandoned / "stale.txt")));
    model.receiveScanBatch(model.activeScanToken_, held);
    QCOMPARE(model.pendingScanEntries_.size(), std::size_t{1});

    model.setPath(QString::fromStdString(destination.string()));
    QCOMPARE(model.pendingScanEntries_.size(), std::size_t{0});
    QVERIFY(waitForIdleWithin(model, 5000));

    QCOMPARE(model.path(), QString::fromStdString(destination.string()));
    QCOMPARE(model.rowCount(), 1);
    QCOMPARE(model.data(model.index(0), DirectoryListModel::NameRole).toString(),
             QStringLiteral("winner.txt"));
    QCOMPARE(rowForName(model, QStringLiteral("stale.txt")), -1);
}

void DirectoryListModelTest::rowKeyIndexTracksEveryRowMutation() {
    QTemporaryDir fixture;
    QVERIFY(fixture.isValid());
    const fs::path root = fixture.path().toStdString();
    writeFile(root / "alpha.txt", "a");
    writeFile(root / "bravo.txt", "bb");
    writeFile(root / "charlie.txt", "ccc");
    writeFile(root / ".hidden.txt", "h");

    DirectoryListModel model;

    // The row mutations are only half the risk. Between a removal and the
    // assignment that ends an update, the rows, their keys, and the key index
    // are all mid-flight, and that is exactly when a view answers the signal
    // by asking the model for data. So the invariant is checked from inside
    // the signals as well as after them.
    QString liveMismatch;
    int liveChecks = 0;
    const auto observe = [&model, &liveMismatch, &liveChecks] {
        ++liveChecks;
        if (liveMismatch.isEmpty()) {
            liveMismatch = rowKeyIndexMismatch(model);
        }
    };
    connect(&model, &QAbstractItemModel::rowsInserted, &model, observe);
    connect(&model, &QAbstractItemModel::rowsRemoved, &model, observe);
    connect(&model, &QAbstractItemModel::layoutChanged, &model, observe);

    model.setPath(fixture.path());
    QTRY_VERIFY_WITH_TIMEOUT(!model.busy(), 5000);
    QCOMPARE(rowKeyIndexMismatch(model), QString{});

    // Reorder.
    model.setSortMode(DirectoryListModel::SortBySize);
    QCOMPARE(rowKeyIndexMismatch(model), QString{});

    // Removal, then insertion, through the filter.
    model.setFilterText(QStringLiteral("alpha"));
    QCOMPARE(model.rowCount(), 1);
    QCOMPARE(rowKeyIndexMismatch(model), QString{});
    model.setFilterText({});
    QCOMPARE(model.rowCount(), 3);
    QCOMPARE(rowKeyIndexMismatch(model), QString{});

    // Insertion of rows the presentation previously withheld.
    model.setShowHidden(true);
    QCOMPARE(model.rowCount(), 4);
    QCOMPARE(rowKeyIndexMismatch(model), QString{});

    // A rescan that replaces every row.
    fs::rename(root / "alpha.txt", root / "delta.txt");
    fs::remove(root / "charlie.txt");
    model.refresh();
    QTRY_VERIFY_WITH_TIMEOUT(!model.busy(), 5000);
    QCOMPARE(rowKeyIndexMismatch(model), QString{});
    QVERIFY(rowForName(model, QStringLiteral("delta.txt")) >= 0);
    QCOMPARE(rowForName(model, QStringLiteral("charlie.txt")), -1);

    // A watch burst that renames and removes in one delivery.
    model.watchService_.stop();
    fs::rename(root / "bravo.txt", root / "echo.txt");
    model.applyWatchUpdate(DirectoryWatchUpdate{
        .token = model.watchToken_,
        .directory = root,
        .removedNames = {"bravo.txt"},
        .updatedEntries = {odysea::core::make_entry(fs::directory_entry(root / "echo.txt"))},
        .renamedEntries = {DirectoryEntryRename{.oldName = "bravo.txt", .newName = "echo.txt"}},
        .error = {},
        .rescanRequired = false});
    QCOMPARE(rowKeyIndexMismatch(model), QString{});
    QVERIFY(rowForName(model, QStringLiteral("echo.txt")) >= 0);

    // An empty listing, so the index is cleared rather than left behind.
    model.setFilterText(QStringLiteral("no-such-entry"));
    QCOMPARE(model.rowCount(), 0);
    QCOMPARE(rowKeyIndexMismatch(model), QString{});

    // Asserted, not assumed: a silent observer that never ran would report a
    // clean tree it had not looked at.
    QVERIFY(liveChecks > 0);
    QCOMPARE(liveMismatch, QString{});
}

void DirectoryListModelTest::duplicateResolvedKeysCompareAgainstTheFirstRow() {
    // A pending rename remap is the one way two presented rows can resolve to
    // a single key: the renamed row resolves to its new key while the row that
    // already holds that key still carries it. Reconciliation compares the
    // presented entry against the first such row, which is what the linear
    // search it replaced returned. Comparing against the last one instead
    // reports an unchanged entry as changed, so views repaint rows that did
    // not move or change.
    //
    // The burst below is delivered to the model directly. A watcher does not
    // produce this shape on its own, which is precisely why the ordering rule
    // needs a test that does not depend on reproducing it from the filesystem.
    QTemporaryDir fixture;
    QVERIFY(fixture.isValid());
    const fs::path root = fixture.path().toStdString();
    writeFile(root / "bravo.txt", "bb");
    writeFile(root / "zulu.txt", "zz");

    DirectoryListModel model;
    model.setPath(fixture.path());
    QTRY_VERIFY_WITH_TIMEOUT(!model.busy(), 5000);
    model.watchService_.stop();
    QCOMPARE(model.rowCount(), 2);
    QCOMPARE(model.data(model.index(0), DirectoryListModel::NameRole).toString(),
             QStringLiteral("bravo.txt"));

    // This burst is also the one case where two rows carry the same key
    // outright rather than only after remapping: the remapped row is
    // reinserted while the original is still present. The row index has to
    // report the first of them while that state is live, which is observable
    // only from inside the insertion signal.
    QString liveMismatch;
    int liveChecks = 0;
    connect(&model, &QAbstractItemModel::rowsInserted, &model,
            [&model, &liveMismatch, &liveChecks] {
                ++liveChecks;
                if (liveMismatch.isEmpty()) {
                    liveMismatch = rowKeyIndexMismatch(model);
                }
            });

    QSignalSpy changedSpy(&model, &QAbstractItemModel::dataChanged);
    model.applyWatchUpdate(DirectoryWatchUpdate{
        .token = model.watchToken_,
        .directory = root,
        .removedNames = {},
        .updatedEntries = {odysea::core::make_entry(fs::directory_entry(root / "bravo.txt"))},
        .renamedEntries = {DirectoryEntryRename{.oldName = "zulu.txt", .newName = "bravo.txt"}},
        .error = {},
        .rescanRequired = false});

    QCOMPARE(model.rowCount(), 2);
    QCOMPARE(changedSpy.count(), 0);
    QCOMPARE(rowKeyIndexMismatch(model), QString{});
    QVERIFY(liveChecks > 0);
    QCOMPARE(liveMismatch, QString{});
}

void DirectoryListModelTest::rapidNavigationCancelsStaleBatches() {
    QTemporaryDir fixture;
    QVERIFY(fixture.isValid());
    const fs::path root = fixture.path().toStdString();
    const fs::path abandoned = root / "abandoned";
    const fs::path destination = root / "destination";
    fs::create_directories(abandoned);
    fs::create_directories(destination);
    for (int index = 0; index < 1800; ++index) {
        writeFile(abandoned / ("entry-" + std::to_string(index)));
    }
    writeFile(destination / "winner.txt");

    DirectoryListModel model;
    model.setPath(QString::fromStdString(abandoned.string()));
    model.setPath(QString::fromStdString(destination.string()));

    QTRY_VERIFY_WITH_TIMEOUT(!model.busy(), 5000);
    QCOMPARE(model.path(), QString::fromStdString(destination.string()));
    QCOMPARE(model.rowCount(), 1);
    QCOMPARE(model.data(model.index(0), DirectoryListModel::NameRole).toString(),
             QStringLiteral("winner.txt"));
}

void DirectoryListModelTest::incrementalScannerPublishesBatches() {
    QTemporaryDir fixture;
    QVERIFY(fixture.isValid());
    const fs::path root = fixture.path().toStdString();
    constexpr int entryCount = 900;
    for (int index = 0; index < entryCount; ++index) {
        writeFile(root / ("entry-" + std::to_string(index)));
    }

    DirectoryListModel model;
    bool sawIncrementalBatch = false;
    QSignalSpy resetSpy(&model, &QAbstractItemModel::modelReset);
    connect(&model, &QAbstractItemModel::rowsInserted, &model, [&] {
        if (model.busy() && model.rowCount() > 0 && model.rowCount() < entryCount) {
            sawIncrementalBatch = true;
        }
    });
    model.setPath(fixture.path());

    QTRY_VERIFY_WITH_TIMEOUT(sawIncrementalBatch, 5000);
    QTRY_VERIFY_WITH_TIMEOUT(!model.busy(), 5000);
    QCOMPARE(model.rowCount(), entryCount);
    QCOMPARE(resetSpy.count(), 0);
}

void DirectoryListModelTest::watchAndPresentationUpdatesUseGranularSignals() {
    QTemporaryDir fixture;
    QVERIFY(fixture.isValid());
    const fs::path root = fixture.path().toStdString();
    writeFile(root / "alpha.txt", "a");
    writeFile(root / "charlie.txt", "ccc");

    DirectoryListModel model;
    model.setPath(fixture.path());
    QTRY_VERIFY_WITH_TIMEOUT(!model.busy(), 5000);
    model.watchService_.stop();

    QSignalSpy resetSpy(&model, &QAbstractItemModel::modelReset);
    QSignalSpy insertedSpy(&model, &QAbstractItemModel::rowsInserted);
    QSignalSpy removedSpy(&model, &QAbstractItemModel::rowsRemoved);
    QSignalSpy changedSpy(&model, &QAbstractItemModel::dataChanged);
    QSignalSpy layoutSpy(&model, &QAbstractItemModel::layoutChanged);

    const int alphaRow = rowForName(model, QStringLiteral("alpha.txt"));
    QVERIFY(alphaRow >= 0);
    model.selectRow(alphaRow, Qt::NoModifier);
    const QPersistentModelIndex persistentAlpha = model.index(alphaRow);

    writeFile(root / "bravo.txt", "bb");
    model.applyWatchUpdate(DirectoryWatchUpdate{
        .token = model.watchToken_,
        .directory = root,
        .removedNames = {},
        .updatedEntries = {odysea::core::make_entry(fs::directory_entry(root / "bravo.txt"))},
        .renamedEntries = {},
        .error = {},
        .rescanRequired = false});
    QCOMPARE(insertedSpy.count(), 1);
    QVERIFY(layoutSpy.count() >= 1);
    QVERIFY(rowForName(model, QStringLiteral("bravo.txt")) >= 0);

    writeFile(root / "bravo.txt", "larger metadata");
    model.applyWatchUpdate(DirectoryWatchUpdate{
        .token = model.watchToken_,
        .directory = root,
        .removedNames = {},
        .updatedEntries = {odysea::core::make_entry(fs::directory_entry(root / "bravo.txt"))},
        .renamedEntries = {},
        .error = {},
        .rescanRequired = false});
    QVERIFY(changedSpy.count() >= 1);

    fs::rename(root / "alpha.txt", root / "delta.txt");
    model.applyWatchUpdate(DirectoryWatchUpdate{
        .token = model.watchToken_,
        .directory = root,
        .removedNames = {"alpha.txt"},
        .updatedEntries = {odysea::core::make_entry(fs::directory_entry(root / "delta.txt"))},
        .renamedEntries = {DirectoryEntryRename{.oldName = "alpha.txt", .newName = "delta.txt"}},
        .error = {},
        .rescanRequired = false});
    QVERIFY(persistentAlpha.isValid());
    QCOMPARE(persistentAlpha.data(DirectoryListModel::NameRole).toString(),
             QStringLiteral("delta.txt"));
    QCOMPARE(selectedName(model), QStringLiteral("delta.txt"));
    QCOMPARE(currentName(model), QStringLiteral("delta.txt"));

    fs::remove(root / "charlie.txt");
    model.applyWatchUpdate(DirectoryWatchUpdate{.token = model.watchToken_,
                                                .directory = root,
                                                .removedNames = {"charlie.txt"},
                                                .updatedEntries = {},
                                                .renamedEntries = {},
                                                .error = {},
                                                .rescanRequired = false});
    QCOMPARE(removedSpy.count(), 1);

    model.setFilterText(QStringLiteral("bravo"));
    QCOMPARE(model.rowCount(), 1);
    model.setFilterText({});
    QCOMPARE(model.rowCount(), 2);

    writeFile(root / ".hidden.txt");
    const qsizetype insertionsBeforeHidden = insertedSpy.count();
    model.applyWatchUpdate(DirectoryWatchUpdate{
        .token = model.watchToken_,
        .directory = root,
        .removedNames = {},
        .updatedEntries = {odysea::core::make_entry(fs::directory_entry(root / ".hidden.txt"))},
        .renamedEntries = {},
        .error = {},
        .rescanRequired = false});
    QCOMPARE(rowForName(model, QStringLiteral(".hidden.txt")), -1);
    QCOMPARE(insertedSpy.count(), insertionsBeforeHidden);
    model.setShowHidden(true);
    QVERIFY(rowForName(model, QStringLiteral(".hidden.txt")) >= 0);
    QCOMPARE(insertedSpy.count(), insertionsBeforeHidden + 1);

    model.setSortMode(DirectoryListModel::SortBySize);
    QVERIFY(layoutSpy.count() >= 2);
    QCOMPARE(resetSpy.count(), 0);
}

void DirectoryListModelTest::navigationHistorySurvivesIncrementalUpdates() {
    QTemporaryDir fixture;
    QVERIFY(fixture.isValid());
    const fs::path root = fixture.path().toStdString();
    const fs::path first = root / "first";
    const fs::path second = root / "second";
    fs::create_directories(first);
    fs::create_directories(second);
    writeFile(first / "one.txt");
    writeFile(second / "two.txt");

    DirectoryListModel model;
    QSignalSpy resetSpy(&model, &QAbstractItemModel::modelReset);
    model.setPath(QString::fromStdString(first.string()));
    QTRY_VERIFY_WITH_TIMEOUT(!model.busy(), 5000);
    model.setPath(QString::fromStdString(second.string()));
    QTRY_VERIFY_WITH_TIMEOUT(!model.busy(), 5000);
    QVERIFY(model.canGoBack());

    writeFile(second / "three.txt");
    QTRY_VERIFY_WITH_TIMEOUT(rowForName(model, QStringLiteral("three.txt")) >= 0, 5000);
    QVERIFY(model.canGoBack());

    model.goBack();
    QTRY_VERIFY_WITH_TIMEOUT(!model.busy(), 5000);
    QCOMPARE(model.path(), QString::fromStdString(first.string()));
    QVERIFY(model.canGoForward());
    QCOMPARE(rowForName(model, QStringLiteral("one.txt")) >= 0, true);
    QCOMPARE(resetSpy.count(), 0);
}

void DirectoryListModelTest::watcherBurstPreservesRenamedSelection() {
    QTemporaryDir fixture;
    QVERIFY(fixture.isValid());
    const fs::path root = fixture.path().toStdString();
    writeFile(root / "selected.txt");

    DirectoryListModel model;
    model.setPath(fixture.path());
    QTRY_VERIFY_WITH_TIMEOUT(!model.busy(), 5000);
    const int selectedRow = rowForName(model, QStringLiteral("selected.txt"));
    QVERIFY(selectedRow >= 0);
    model.selectRow(selectedRow, Qt::NoModifier);

    writeFile(root / "created-a.txt");
    writeFile(root / "created-b.txt");
    fs::rename(root / "selected.txt", root / "renamed.txt");
    fs::remove(root / "created-a.txt");

    QTRY_VERIFY_WITH_TIMEOUT(rowForName(model, QStringLiteral("renamed.txt")) >= 0, 5000);
    QTRY_VERIFY_WITH_TIMEOUT(rowForName(model, QStringLiteral("created-b.txt")) >= 0, 5000);
    QTRY_COMPARE_WITH_TIMEOUT(rowForName(model, QStringLiteral("created-a.txt")), -1, 5000);
    QCOMPARE(model.selectedCount(), 1);
    QCOMPARE(selectedName(model), QStringLiteral("renamed.txt"));
}

void DirectoryListModelTest::hardLinksRemainDistinctAcrossRefreshAndRename() {
    QTemporaryDir fixture;
    QVERIFY(fixture.isValid());
    const fs::path root = fixture.path().toStdString();
    writeFile(root / "first.txt", "shared");
    fs::create_hard_link(root / "first.txt", root / "second.txt");

    DirectoryListModel model;
    model.setPath(fixture.path());
    QTRY_VERIFY_WITH_TIMEOUT(!model.busy(), 5000);
    model.selectRow(rowForName(model, QStringLiteral("first.txt")), Qt::NoModifier);
    QCOMPARE(model.selectedCount(), 1);
    QCOMPARE(selectedName(model), QStringLiteral("first.txt"));
    QCOMPARE(currentName(model), QStringLiteral("first.txt"));

    model.setSortMode(DirectoryListModel::SortBySize);
    model.refresh();
    QTRY_VERIFY_WITH_TIMEOUT(!model.busy(), 5000);
    QCOMPARE(model.selectedCount(), 1);
    QCOMPARE(selectedName(model), QStringLiteral("first.txt"));
    QCOMPARE(currentName(model), QStringLiteral("first.txt"));

    writeFile(root / "unrelated.txt");
    QTRY_VERIFY_WITH_TIMEOUT(rowForName(model, QStringLiteral("unrelated.txt")) >= 0, 5000);
    QCOMPARE(model.selectedCount(), 1);
    QCOMPARE(selectedName(model), QStringLiteral("first.txt"));
    QCOMPARE(currentName(model), QStringLiteral("first.txt"));

    fs::rename(root / "second.txt", root / "alternate.txt");
    QTRY_VERIFY_WITH_TIMEOUT(rowForName(model, QStringLiteral("alternate.txt")) >= 0, 5000);
    QCOMPARE(model.selectedCount(), 1);
    QCOMPARE(selectedName(model), QStringLiteral("first.txt"));
    QCOMPARE(currentName(model), QStringLiteral("first.txt"));

    fs::rename(root / "first.txt", root / "renamed.txt");
    QTRY_VERIFY_WITH_TIMEOUT(rowForName(model, QStringLiteral("renamed.txt")) >= 0, 5000);
    QCOMPARE(model.selectedCount(), 1);
    QCOMPARE(selectedName(model), QStringLiteral("renamed.txt"));
    QCOMPARE(currentName(model), QStringLiteral("renamed.txt"));
}

void DirectoryListModelTest::uniqueInodeFallbackPreservesRename() {
    QTemporaryDir fixture;
    QVERIFY(fixture.isValid());
    const fs::path root = fixture.path().toStdString();
    writeFile(root / "before.txt");

    DirectoryListModel model;
    model.setPath(fixture.path());
    QTRY_VERIFY_WITH_TIMEOUT(!model.busy(), 5000);
    model.selectRow(rowForName(model, QStringLiteral("before.txt")), Qt::NoModifier);
    model.watchService_.stop();

    fs::rename(root / "before.txt", root / "after.txt");
    const odysea::core::Entry renamed =
        odysea::core::make_entry(fs::directory_entry(root / "after.txt"));
    model.applyWatchUpdate(DirectoryWatchUpdate{.token = model.watchToken_,
                                                .directory = root,
                                                .removedNames = {"before.txt"},
                                                .updatedEntries = {renamed},
                                                .renamedEntries = {},
                                                .error = {},
                                                .rescanRequired = false});

    QCOMPARE(model.selectedCount(), 1);
    QCOMPARE(selectedName(model), QStringLiteral("after.txt"));
    QCOMPARE(currentName(model), QStringLiteral("after.txt"));

    fs::rename(root / "after.txt", root / "final.txt");
    model.refresh();
    QTRY_VERIFY_WITH_TIMEOUT(!model.busy(), 5000);
    QCOMPARE(model.selectedCount(), 1);
    QCOMPARE(selectedName(model), QStringLiteral("final.txt"));
    QCOMPARE(currentName(model), QStringLiteral("final.txt"));
}

// Reconciliation follows an entry across a rename by matching identity, and it
// only does so when the match is unambiguous. That guard counts occurrences,
// so it cannot help when a genuinely different entry presents the identity a
// departed one had: both sides count exactly one and the match looks clean.
//
// Btrfs produces exactly that. A subvolume root always carries inode 256, and
// its device number is an anonymous one the kernel returns to a pool when the
// subvolume goes away and reissues to the next subvolume created. Removing one
// subvolume and creating another therefore reproduces the earlier pair. The
// pair is injected here rather than driven through a real Btrfs mount, because
// the aliasing is a property of the identity function and provoking it for
// real needs both a Btrfs filesystem and the privilege to manipulate
// subvolumes. Selection must be dropped, never moved onto the newcomer.
void DirectoryListModelTest::recycledSubvolumeIdentityDoesNotMoveSelection() {
    QTemporaryDir fixture;
    QVERIFY(fixture.isValid());
    const fs::path root = fixture.path().toStdString();
    fs::create_directory(root / "alpha");

    DirectoryListModel model;
    model.setPath(fixture.path());
    QTRY_VERIFY_WITH_TIMEOUT(!model.busy(), 5000);
    model.selectRow(rowForName(model, QStringLiteral("alpha")), Qt::NoModifier);
    QCOMPARE(selectedName(model), QStringLiteral("alpha"));
    model.watchService_.stop();

    // Read the baseline identity from the scanned entries rather than by row,
    // because presentation order is not scan order.
    const auto scanned =
        std::ranges::find(model.scannedEntries_, "alpha", &odysea::core::Entry::name);
    QVERIFY(scanned != model.scannedEntries_.end());
    const odysea::core::EntryIdentity departed = scanned->identity;
    QVERIFY(departed.known());

    fs::remove(root / "alpha");
    fs::create_directory(root / "gamma");
    odysea::core::Entry arrival = odysea::core::make_entry(fs::directory_entry(root / "gamma"));
    QVERIFY(arrival.identity.known());
    QVERIFY(!odysea::core::same_identity(arrival.identity, departed));

    // Recycle the departed entry's device and inode onto the newcomer, exactly
    // as the kernel does, and leave every other property its own.
    arrival.identity.device = departed.device;
    arrival.identity.inode = departed.inode;
    QCOMPARE(arrival.identity.device, departed.device);
    QCOMPARE(arrival.identity.inode, departed.inode);

    model.applyWatchUpdate(DirectoryWatchUpdate{.token = model.watchToken_,
                                                .directory = root,
                                                .removedNames = {"alpha"},
                                                .updatedEntries = {arrival},
                                                .renamedEntries = {},
                                                .error = {},
                                                .rescanRequired = false});

    QCOMPARE(rowForName(model, QStringLiteral("alpha")), -1);
    QVERIFY(rowForName(model, QStringLiteral("gamma")) >= 0);
    QCOMPARE(selectedName(model), QString());
    QCOMPARE(model.selectedCount(), 0);
    QCOMPARE(currentName(model), QString());
}

// The other half of the guard. Identity is deliberately shared by hard links,
// because they are the same file, but they are separate entries a user selects
// independently. When a departed entry's identity still matches more than one
// entry, there is no single answer to "where did it go", so selection is
// dropped rather than guessed. Without the uniqueness requirement the search
// would simply take the first match and move selection onto the other name.
void DirectoryListModelTest::ambiguousIdentityDoesNotMoveSelection() {
    QTemporaryDir fixture;
    QVERIFY(fixture.isValid());
    const fs::path root = fixture.path().toStdString();
    writeFile(root / "first.txt");
    fs::create_hard_link(root / "first.txt", root / "second.txt");

    DirectoryListModel model;
    model.setPath(fixture.path());
    QTRY_VERIFY_WITH_TIMEOUT(!model.busy(), 5000);
    model.selectRow(rowForName(model, QStringLiteral("first.txt")), Qt::NoModifier);
    QCOMPARE(selectedName(model), QStringLiteral("first.txt"));
    model.watchService_.stop();

    // The fixture is only meaningful if the two names really do share one
    // identity; otherwise the uniqueness requirement is never consulted.
    const auto first =
        std::ranges::find(model.scannedEntries_, "first.txt", &odysea::core::Entry::name);
    const auto second =
        std::ranges::find(model.scannedEntries_, "second.txt", &odysea::core::Entry::name);
    QVERIFY(first != model.scannedEntries_.end());
    QVERIFY(second != model.scannedEntries_.end());
    QVERIFY(odysea::core::same_identity(first->identity, second->identity));

    const odysea::core::Entry& survivor = *second;
    fs::remove(root / "first.txt");
    model.applyWatchUpdate(DirectoryWatchUpdate{.token = model.watchToken_,
                                                .directory = root,
                                                .removedNames = {"first.txt"},
                                                .updatedEntries = {survivor},
                                                .renamedEntries = {},
                                                .error = {},
                                                .rescanRequired = false});

    QCOMPARE(rowForName(model, QStringLiteral("first.txt")), -1);
    QVERIFY(rowForName(model, QStringLiteral("second.txt")) >= 0);
    QCOMPARE(selectedName(model), QString());
    QCOMPARE(model.selectedCount(), 0);
}

// Reconciliation happens on two paths, and both have to reject a recycled
// identity. The watcher path compares identities directly; a completed rescan
// groups entries by a hashed spelling of the identity instead, because it
// reconciles whole listings at once. A spelling that omitted any part of the
// identity would collapse a recycled pair back onto the entry that held it,
// which is why this exercise drives a full refresh rather than a watch update.
void DirectoryListModelTest::recycledIdentityDoesNotMoveSelectionAcrossRefresh() {
    QTemporaryDir fixture;
    QVERIFY(fixture.isValid());
    const fs::path root = fixture.path().toStdString();
    fs::create_directory(root / "alpha");

    DirectoryListModel model;
    model.setPath(fixture.path());
    QTRY_VERIFY_WITH_TIMEOUT(!model.busy(), 5000);
    model.selectRow(rowForName(model, QStringLiteral("alpha")), Qt::NoModifier);
    QCOMPARE(selectedName(model), QStringLiteral("alpha"));

    fs::remove(root / "alpha");
    fs::create_directory(root / "gamma");
    const odysea::core::Entry arrival =
        odysea::core::make_entry(fs::directory_entry(root / "gamma"));
    QVERIFY(arrival.identity.known());
    if (!arrival.identity.birth_known) {
        // Separating a recycled pair depends on the filesystem recording a
        // creation time. Where it records none, identity degrades to the pair
        // by design and this exercise has nothing to assert. Skipping says so
        // out loud instead of passing as though the guarantee held.
        QSKIP("the temporary filesystem records no creation time");
    }

    // Give the departed entry the device and inode numbers the newcomer will
    // report, exactly as the kernel does when it reissues them. The creation
    // times are pinned to the same second and separated only in nanoseconds,
    // which is what a real removal and creation produce: the two measured
    // roughly two milliseconds apart. Relying on the fixture's own timing
    // instead would leave the exercise passing for the wrong reason whenever
    // the two creations happened to land in different seconds.
    const auto departed =
        std::ranges::find(model.scannedEntries_, "alpha", &odysea::core::Entry::name);
    QVERIFY(departed != model.scannedEntries_.end());
    departed->identity.device = arrival.identity.device;
    departed->identity.inode = arrival.identity.inode;
    departed->identity.birth_seconds = arrival.identity.birth_seconds;
    departed->identity.birth_nanoseconds = arrival.identity.birth_nanoseconds + 1;
    QCOMPARE(departed->identity.birth_seconds, arrival.identity.birth_seconds);
    QVERIFY(!odysea::core::same_identity(departed->identity, arrival.identity));

    model.refresh();
    QTRY_VERIFY_WITH_TIMEOUT(!model.busy(), 5000);

    QCOMPARE(rowForName(model, QStringLiteral("alpha")), -1);
    QVERIFY(rowForName(model, QStringLiteral("gamma")) >= 0);
    QCOMPARE(selectedName(model), QString());
    QCOMPARE(model.selectedCount(), 0);
}

void DirectoryListModelTest::selectionSurvivesSortFilterAndRefresh() {
    QTemporaryDir fixture;
    QVERIFY(fixture.isValid());
    const fs::path root = fixture.path().toStdString();
    writeFile(root / "small.txt", "x");
    writeFile(root / "large.txt", "xxxxxxxx");

    DirectoryListModel model;
    model.setPath(fixture.path());
    QTRY_VERIFY_WITH_TIMEOUT(!model.busy(), 5000);
    const int selectedRow = rowForName(model, QStringLiteral("small.txt"));
    QVERIFY(selectedRow >= 0);
    model.selectRow(selectedRow, Qt::NoModifier);

    model.setSortMode(DirectoryListModel::SortBySize);
    QCOMPARE(selectedName(model), QStringLiteral("small.txt"));

    model.setFilterText(QStringLiteral("large"));
    QCOMPARE(model.selectedCount(), 0);
    model.setFilterText({});
    QCOMPARE(model.selectedCount(), 1);
    QCOMPARE(selectedName(model), QStringLiteral("small.txt"));

    model.refresh();
    QTRY_VERIFY_WITH_TIMEOUT(!model.busy(), 5000);
    QCOMPARE(model.selectedCount(), 1);
    QCOMPARE(selectedName(model), QStringLiteral("small.txt"));
}

void DirectoryListModelTest::explicitGeometricSelectionUsesRowSet() {
    QTemporaryDir fixture;
    QVERIFY(fixture.isValid());
    const fs::path root = fixture.path().toStdString();
    for (int row = 0; row < 5; ++row) {
        writeFile(root / ("entry-" + std::to_string(row) + ".txt"),
                  std::string(static_cast<std::size_t>(5 - row), 'x'));
    }

    DirectoryListModel model;
    model.setPath(fixture.path());
    QTRY_VERIFY_WITH_TIMEOUT(!model.busy(), 5000);

    model.beginRubberBand(false);
    model.updateRubberBandSelection(QVariantList{0, 2, 4, 4, -1, 99}, 2);
    model.endRubberBand();
    QCOMPARE(selectedNames(model),
             QStringList({QStringLiteral("entry-0.txt"), QStringLiteral("entry-2.txt"),
                          QStringLiteral("entry-4.txt")}));
    QCOMPARE(model.currentIndex(), 2);

    model.selectRow(0, Qt::NoModifier);
    model.selectRow(4, Qt::ShiftModifier);
    model.setSortMode(DirectoryListModel::SortBySize);
    model.selectRow(rowForName(model, QStringLiteral("entry-1.txt")), Qt::ShiftModifier);
    QCOMPARE(selectedNames(model),
             QStringList({QStringLiteral("entry-1.txt"), QStringLiteral("entry-0.txt")}));

    model.setSortMode(DirectoryListModel::SortByName);
    model.selectRow(1, Qt::NoModifier);
    model.beginRubberBand(true);
    model.updateRubberBandSelection(QVariantList{3}, 3);
    model.endRubberBand();
    QCOMPARE(selectedNames(model),
             QStringList({QStringLiteral("entry-1.txt"), QStringLiteral("entry-3.txt")}));
    QCOMPARE(model.currentIndex(), 3);

    model.beginRubberBand(false);
    model.updateRubberBandSelection({}, -1);
    model.endRubberBand();
    QCOMPARE(model.selectedCount(), 0);
}

void DirectoryListModelTest::cursorMovementAndPrefixSearchPreserveSelectionContracts() {
    QTemporaryDir fixture;
    QVERIFY(fixture.isValid());
    const fs::path root = fixture.path().toStdString();
    for (const std::string_view name :
         {"alpha.txt", "Alpine.md", "beta.txt", "bravo.txt", "zulu.txt"}) {
        writeFile(root / name);
    }

    DirectoryListModel model;
    model.setPath(fixture.path());
    QTRY_VERIFY_WITH_TIMEOUT(!model.busy(), 5000);
    QCOMPARE(model.rowCount(), 5);

    model.selectRow(rowForName(model, QStringLiteral("beta.txt")), Qt::NoModifier);
    QVERIFY(model.selectByPrefix(QStringLiteral("AL"), false));
    QCOMPARE(currentName(model), QStringLiteral("alpha.txt"));
    QCOMPARE(selectedName(model), QStringLiteral("alpha.txt"));

    QVERIFY(model.selectByPrefix(QStringLiteral("al"), true));
    QCOMPARE(currentName(model), QStringLiteral("Alpine.md"));
    QVERIFY(model.selectByPrefix(QStringLiteral("al"), true));
    QCOMPARE(currentName(model), QStringLiteral("alpha.txt"));

    QVERIFY(!model.selectByPrefix(QStringLiteral("missing"), true));
    QCOMPARE(currentName(model), QStringLiteral("alpha.txt"));
    QCOMPARE(selectedName(model), QStringLiteral("alpha.txt"));

    QVERIFY(model.selectByPrefix(QStringLiteral("br"), false));
    QCOMPARE(currentName(model), QStringLiteral("bravo.txt"));
    model.moveCursorTo(rowForName(model, QStringLiteral("zulu.txt")), false, true);
    QCOMPARE(currentName(model), QStringLiteral("zulu.txt"));
    QCOMPARE(selectedName(model), QStringLiteral("bravo.txt"));
    model.moveCursorTo(rowForName(model, QStringLiteral("beta.txt")), true, false);
    QCOMPARE(selectedNames(model),
             QStringList({QStringLiteral("beta.txt"), QStringLiteral("bravo.txt")}));

    model.moveCursorTo(rowForName(model, QStringLiteral("alpha.txt")), false, false);
    model.moveCursor(1, false, true);
    QCOMPARE(currentName(model), QStringLiteral("Alpine.md"));
    QCOMPARE(selectedName(model), QStringLiteral("alpha.txt"));
    model.moveCursorTo(rowForName(model, QStringLiteral("beta.txt")), true, false);
    QCOMPARE(selectedNames(model),
             QStringList({QStringLiteral("alpha.txt"), QStringLiteral("Alpine.md"),
                          QStringLiteral("beta.txt")}));
}

void DirectoryListModelTest::sameParentCopyCreatesSiblingDuplicate() {
    QTemporaryDir fixture;
    QVERIFY(fixture.isValid());
    const fs::path root = fixture.path().toStdString();
    const fs::path source = root / "source";
    const fs::path document = source / "document.txt";
    fs::create_directories(source);
    writeFile(document);

    DirectoryListModel model;
    model.setPath(QString::fromStdString(source.string()));
    QVERIFY(waitForScan(model));

    const int documentRow = rowForName(model, QStringLiteral("document.txt"));
    QVERIFY(documentRow >= 0);
    model.selectRow(documentRow, Qt::NoModifier);
    QVERIFY(model.canDropSelection(QString::fromStdString(source.string()), false));
    QVERIFY(!model.canDropSelection(QString::fromStdString(source.string()), true));
    QVERIFY(model.dropSelection(QString::fromStdString(source.string()), false,
                                DirectoryListModel::ConflictFail));
    QVERIFY(waitForOperation(model));
    QVERIFY(fs::exists(source / "document (2).txt"));
}

void DirectoryListModelTest::directDirectoryActivationNavigates() {
    QTemporaryDir fixture;
    QVERIFY(fixture.isValid());
    const fs::path root = fixture.path().toStdString();
    const fs::path source = root / "source";
    const fs::path folder = source / "folder";
    fs::create_directories(folder);

    DirectoryListModel model;
    model.setPath(QString::fromStdString(source.string()));
    QVERIFY(waitForScan(model));

    const int folderRow = rowForName(model, QStringLiteral("folder"));
    QVERIFY(folderRow >= 0);
    model.activate(folderRow);
    QVERIFY(waitForScan(model));
    QCOMPARE(model.path(), QString::fromStdString(folder.string()));
}

void DirectoryListModelTest::navigationInputResolvesTilde() {
    QCOMPARE(DirectoryListModel::resolveNavigationInput(QStringLiteral("~")), QDir::homePath());
    QCOMPARE(DirectoryListModel::resolveNavigationInput(QStringLiteral("~/synthetic")),
             QDir::cleanPath(QDir::homePath() + QStringLiteral("/synthetic")));
    QCOMPARE(DirectoryListModel::navigationCompletion(QStringLiteral("~"))
                 .value(QStringLiteral("completed"))
                 .toString(),
             QStringLiteral("~/"));
    QVERIFY(
        DirectoryListModel::resolveNavigationInput(QStringLiteral("~someone/elsewhere")).isEmpty());
}

void DirectoryListModelTest::directPathInputValidatesDirectory() {
    QTemporaryDir fixture;
    QVERIFY(fixture.isValid());
    const fs::path root = fixture.path().toStdString();
    const fs::path folder = root / "folder";
    fs::create_directories(folder);

    DirectoryListModel model;
    model.setPath(fixture.path());
    QVERIFY(waitForScan(model));
    QVERIFY(model.navigateFromInput(QString::fromStdString(folder.string())));
    QVERIFY(waitForScan(model));
    QCOMPARE(model.path(), QString::fromStdString(folder.string()));
    QVERIFY(model.statusMessage().startsWith(QStringLiteral("Opened ")));
}

void DirectoryListModelTest::invalidDirectPathInputDoesNotNavigate() {
    QTemporaryDir fixture;
    QVERIFY(fixture.isValid());
    const fs::path root = fixture.path().toStdString();
    const fs::path document = root / "document.txt";
    writeFile(document);

    DirectoryListModel model;
    model.setPath(fixture.path());
    QVERIFY(waitForScan(model));
    QVERIFY(!model.navigateFromInput(QStringLiteral("relative/path")));
    QVERIFY(model.statusMessage().contains(QStringLiteral("absolute path")));
    QVERIFY(!model.navigateFromInput(QString::fromStdString(document.string())));
    QVERIFY(model.statusMessage().contains(QStringLiteral("reachable directory")));
    QVERIFY(!model.navigateFromInput(QString::fromStdString((root / "missing").string())));
    QCOMPARE(model.path(), fixture.path());
}

void DirectoryListModelTest::sharedPathCompletionFinishesTheNextSegmentPrefix() {
    QTemporaryDir fixture;
    QVERIFY(fixture.isValid());
    const fs::path root = fixture.path().toStdString();
    fs::create_directories(root / "profile");
    fs::create_directories(root / "projects");
    fs::create_directories(root / "prose");
    fs::create_directories(root / ".private");
    writeFile(root / "project-file.txt");

    const QString prefix = fixture.path() + QStringLiteral("/pr");
    const QVariantMap shared = DirectoryListModel::navigationCompletion(prefix);
    QCOMPARE(shared.value(QStringLiteral("completed")).toString(),
             fixture.path() + QStringLiteral("/pro"));
    QCOMPARE(shared.value(QStringLiteral("suffix")).toString(), QStringLiteral("o"));
    QCOMPARE(shared.value(QStringLiteral("candidates")).toStringList(),
             QStringList(
                 {QStringLiteral("profile"), QStringLiteral("projects"), QStringLiteral("prose")}));
}

void DirectoryListModelTest::uniquePathCompletionFinishesTheDirectoryName() {
    QTemporaryDir fixture;
    QVERIFY(fixture.isValid());
    const fs::path root = fixture.path().toStdString();
    fs::create_directories(root / "projects");
    fs::create_directories(root / ".private");
    writeFile(root / "project-file.txt");

    const QString uniquePrefix = fixture.path() + QStringLiteral("/proj");
    const QVariantMap unique = DirectoryListModel::navigationCompletion(uniquePrefix);
    QCOMPARE(unique.value(QStringLiteral("completed")).toString(),
             fixture.path() + QStringLiteral("/projects/"));
    QCOMPARE(unique.value(QStringLiteral("suffix")).toString(), QStringLiteral("ects/"));
    QCOMPARE(unique.value(QStringLiteral("candidates")).toStringList(),
             QStringList{QStringLiteral("projects")});

    const QVariantMap hidden =
        DirectoryListModel::navigationCompletion(fixture.path() + QStringLiteral("/.p"));
    QCOMPARE(hidden.value(QStringLiteral("completed")).toString(),
             fixture.path() + QStringLiteral("/.private/"));
    QVERIFY(DirectoryListModel::navigationCompletion(QStringLiteral("relative"))
                .value(QStringLiteral("candidates"))
                .toStringList()
                .isEmpty());
}

void DirectoryListModelTest::activationBreadcrumbsAndDropContracts() {
    QTemporaryDir fixture;
    QVERIFY(fixture.isValid());
    const fs::path root = fixture.path().toStdString();
    const fs::path source = root / "source space";
    const fs::path destination = root / "destination";
    const fs::path folder = source / "folder";
    const fs::path descendant = folder / "descendant";
    const fs::path folderLink = source / "folder-link";
    const fs::path remoteTarget = destination / "faraway";
    const fs::path remoteLink = source / "remote-link";
    const fs::path document = source / "document.txt";
    fs::create_directories(descendant);
    fs::create_directories(remoteTarget);
    writeFile(document);
    writeFile(remoteTarget / "remote-document.txt");
    std::error_code linkError;
    fs::create_directory_symlink(folder, folderLink, linkError);
    QVERIFY2(!linkError, linkError.message().c_str());
    fs::create_directory_symlink(remoteTarget, remoteLink, linkError);
    QVERIFY2(!linkError, linkError.message().c_str());

    FakeEntryLauncher launcher;
    DirectoryListModel model(launcher);
    model.setPath(QString::fromStdString(source.string()));
    QTRY_VERIFY_WITH_TIMEOUT(!model.busy(), 5000);

    const QVariantList breadcrumbs = model.breadcrumbSegments();
    QVERIFY(breadcrumbs.size() >= 2);
    QCOMPARE(breadcrumbs.front().toMap().value(QStringLiteral("path")).toString(),
             QStringLiteral("/"));
    QCOMPARE(breadcrumbs.back().toMap().value(QStringLiteral("label")).toString(),
             QStringLiteral("source space"));
    QCOMPARE(breadcrumbs.back().toMap().value(QStringLiteral("path")).toString(),
             QString::fromStdString(source.string()));
    QCOMPARE(breadcrumbs.back().toMap().value(QStringLiteral("url")).toString(),
             QUrl::fromLocalFile(QString::fromStdString(source.string())).toString());

    const int documentRow = rowForName(model, QStringLiteral("document.txt"));
    QVERIFY(documentRow >= 0);
    model.selectRow(documentRow, Qt::NoModifier);
    QVERIFY(model.rowSelected(documentRow));
    QVERIFY(!model.rowIsDirectory(documentRow));
    QCOMPARE(model.selectedFileUrls(),
             QStringList{QUrl::fromLocalFile(QString::fromStdString(document.string()))
                             .toString(QUrl::FullyEncoded)});
    QSignalSpy openSpy(&model, &DirectoryListModel::openRequested);
    model.activateCurrent();
    QCOMPARE(launcher.callCount, 1);
    QCOMPARE(launcher.openedPath, document);
    QCOMPARE(openSpy.count(), 1);
    QVERIFY(model.statusMessage().startsWith(QStringLiteral("Opened ")));

    launcher.fail = true;
    model.activate(documentRow);
    QCOMPARE(launcher.callCount, 2);
    QCOMPARE(openSpy.count(), 2);
    QVERIFY(model.statusMessage().startsWith(QStringLiteral("Could not open ")));

    QVERIFY(model.canDropSelection(QString::fromStdString(destination.string()), false));
    QVERIFY(model.dropSelection(QString::fromStdString(destination.string()), false,
                                DirectoryListModel::ConflictFail));
    QTRY_VERIFY_WITH_TIMEOUT(!model.operationBusy(), 5000);
    QVERIFY(fs::exists(destination / "document.txt"));

    QTRY_VERIFY_WITH_TIMEOUT(rowForName(model, QStringLiteral("folder-link")) >= 0, 5000);
    const int remoteLinkRow = rowForName(model, QStringLiteral("remote-link"));
    QVERIFY(remoteLinkRow >= 0);
    model.selectRow(remoteLinkRow, Qt::NoModifier);
    QVERIFY(model.canDropSelection(QString::fromStdString(source.string()), false));
    QVERIFY(!model.canDropSelection(QString::fromStdString(source.string()), true));

    const int folderLinkRow = rowForName(model, QStringLiteral("folder-link"));
    QVERIFY(model.rowIsDirectory(folderLinkRow));
    QVERIFY(model.data(model.index(folderLinkRow), DirectoryListModel::IsDirRole).toBool());
    model.selectRow(documentRow, Qt::NoModifier);
    QVERIFY(model.canDropSelection(QString::fromStdString(folderLink.string()), false));
    model.activate(folderLinkRow);
    QTRY_VERIFY_WITH_TIMEOUT(!model.busy(), 5000);
    QCOMPARE(model.path(), QString::fromStdString(folderLink.string()));
    QCOMPARE(launcher.callCount, 2);

    model.navigateToPath(QString::fromStdString(source.string()));
    QTRY_VERIFY_WITH_TIMEOUT(!model.busy(), 5000);
    QTRY_VERIFY_WITH_TIMEOUT(rowForName(model, QStringLiteral("folder")) >= 0, 5000);
    const int folderRow = rowForName(model, QStringLiteral("folder"));
    QVERIFY(model.rowIsDirectory(folderRow));
    model.selectRow(folderRow, Qt::NoModifier);
    QVERIFY(!model.canDropSelection(QString::fromStdString(folder.string()), false));
    QVERIFY(!model.canDropSelection(QString::fromStdString(descendant.string()), false));
    QVERIFY(!model.dropSelection(QString::fromStdString(descendant.string()), true,
                                 DirectoryListModel::ConflictFail));
    QVERIFY(model.statusMessage().contains(QStringLiteral("cannot be transferred")));

    model.navigateToPath(QString::fromStdString(folder.string()));
    QTRY_VERIFY_WITH_TIMEOUT(!model.busy(), 5000);
    QCOMPARE(model.path(), QString::fromStdString(folder.string()));
    QCOMPARE(launcher.callCount, 2);

    model.navigateToPath(QString::fromStdString(remoteTarget.string()));
    QTRY_VERIFY_WITH_TIMEOUT(!model.busy(), 5000);
    const int remoteDocumentRow = rowForName(model, QStringLiteral("remote-document.txt"));
    QVERIFY(remoteDocumentRow >= 0);
    model.selectRow(remoteDocumentRow, Qt::NoModifier);
    QVERIFY(model.canDropSelection(QString::fromStdString(remoteLink.string()), false));
    QVERIFY(!model.canDropSelection(QString::fromStdString(remoteLink.string()), true));
}

void DirectoryListModelTest::symlinkTargetDirectoryChangesRefreshRole() {
    QTemporaryDir fixture;
    QVERIFY(fixture.isValid());
    const fs::path root = fixture.path().toStdString();
    const fs::path link = root / "changing-link";
    std::error_code linkError;
    fs::create_symlink(root / "missing-target", link, linkError);
    QVERIFY2(!linkError, linkError.message().c_str());

    DirectoryListModel model;
    model.setPath(fixture.path());
    QTRY_VERIFY_WITH_TIMEOUT(!model.busy(), 5000);
    model.watchService_.stop();

    const int linkRow = rowForName(model, QStringLiteral("changing-link"));
    QVERIFY(linkRow >= 0);
    QVERIFY(!model.data(model.index(linkRow), DirectoryListModel::IsDirRole).toBool());
    QVERIFY(model.data(model.index(linkRow), DirectoryListModel::IsSymlinkRole).toBool());

    const auto scannedLink = std::ranges::find(model.scannedEntries_, std::string{"changing-link"},
                                               &odysea::core::Entry::name);
    QVERIFY(scannedLink != model.scannedEntries_.end());
    odysea::core::Entry updated = *scannedLink;
    updated.target_is_directory = true;

    QSignalSpy changedSpy(&model, &QAbstractItemModel::dataChanged);
    model.applyWatchUpdate(DirectoryWatchUpdate{.token = model.watchToken_,
                                                .directory = root,
                                                .removedNames = {},
                                                .updatedEntries = {std::move(updated)},
                                                .renamedEntries = {},
                                                .error = {},
                                                .rescanRequired = false});

    QCOMPARE(changedSpy.count(), 1);
    QVERIFY(model.data(model.index(linkRow), DirectoryListModel::IsDirRole).toBool());
    QVERIFY(model.data(model.index(linkRow), DirectoryListModel::IsSymlinkRole).toBool());
}

void DirectoryListModelTest::operationsReachCoreAndReportFailures() {
    QTemporaryDir fixture;
    QVERIFY(fixture.isValid());
    const fs::path root = fixture.path().toStdString();
    const fs::path source = root / "source";
    const fs::path destination = root / "destination";
    const fs::path dataHome = root / "data";
    fs::create_directories(source);
    fs::create_directories(destination);
    fs::create_directories(dataHome);
    writeFile(source / "copy.txt");
    writeFile(source / "move.txt");
    writeFile(source / "rename.txt");
    writeFile(source / "trash.txt");

    EnvironmentRestore restoreDataHome("XDG_DATA_HOME");
    EnvironmentRestore restoreHome("HOME");
    qputenv("XDG_DATA_HOME", QByteArray::fromStdString(dataHome.string()));
    qunsetenv("HOME");

    DirectoryListModel model;
    model.setPath(QString::fromStdString(source.string()));
    QTRY_VERIFY_WITH_TIMEOUT(!model.busy(), 5000);

    model.selectRow(rowForName(model, QStringLiteral("copy.txt")), Qt::NoModifier);
    model.performCopy(QString::fromStdString(destination.string()),
                      DirectoryListModel::ConflictFail);
    QTRY_VERIFY_WITH_TIMEOUT(!model.operationBusy(), 5000);
    QVERIFY(fs::exists(destination / "copy.txt"));
    QVERIFY(model.operationErrorString().isEmpty());

    QTRY_VERIFY_WITH_TIMEOUT(rowForName(model, QStringLiteral("move.txt")) >= 0, 5000);
    model.selectRow(rowForName(model, QStringLiteral("move.txt")), Qt::NoModifier);
    model.performMove(QString::fromStdString(destination.string()),
                      DirectoryListModel::ConflictFail);
    QTRY_VERIFY_WITH_TIMEOUT(!model.operationBusy(), 5000);
    QVERIFY(!fs::exists(source / "move.txt"));
    QVERIFY(fs::exists(destination / "move.txt"));

    QTRY_VERIFY_WITH_TIMEOUT(rowForName(model, QStringLiteral("rename.txt")) >= 0, 5000);
    model.selectRow(rowForName(model, QStringLiteral("rename.txt")), Qt::NoModifier);
    model.performRename(QStringLiteral("renamed.txt"), DirectoryListModel::ConflictFail);
    QTRY_VERIFY_WITH_TIMEOUT(!model.operationBusy(), 5000);
    QVERIFY(fs::exists(source / "renamed.txt"));

    QTRY_VERIFY_WITH_TIMEOUT(rowForName(model, QStringLiteral("trash.txt")) >= 0, 5000);
    model.selectRow(rowForName(model, QStringLiteral("trash.txt")), Qt::NoModifier);
    model.performTrash();
    QTRY_VERIFY_WITH_TIMEOUT(!model.operationBusy(), 5000);
    QVERIFY(!fs::exists(source / "trash.txt"));
    QVERIFY(fs::exists(dataHome / "Trash" / "files" / "trash.txt"));

    QTRY_VERIFY_WITH_TIMEOUT(rowForName(model, QStringLiteral("renamed.txt")) >= 0, 5000);
    model.selectRow(rowForName(model, QStringLiteral("renamed.txt")), Qt::NoModifier);
    model.performCopy(QString::fromStdString((root / "missing").string()),
                      DirectoryListModel::ConflictFail);
    QTRY_VERIFY_WITH_TIMEOUT(!model.operationBusy(), 5000);
    QVERIFY(!model.operationErrorString().isEmpty());
}

void DirectoryListModelTest::retainedRecoveryRemainsVisibleDuringOperation() {
    QTemporaryDir fixture;
    QVERIFY(fixture.isValid());
    const fs::path root = fixture.path().toStdString();
    const fs::path source = root / "source" / "project";
    const fs::path target = root / "target";
    fs::create_directories(source);
    fs::create_directories(target / "project");
    writeFile(source / "kept.txt", "only copy");
    writeFile(target / "project" / "existing.txt", "existing");

    const odysea::core::OperationOutcome outcome = odysea::core::detail::move_into_using(
        source, target, {.conflict = odysea::core::ConflictPolicy::Overwrite},
        failInstallAndUnwind());
    QVERIFY(!outcome.succeeded());
    QVERIFY(!fs::exists(source));
    const fs::path retained = workingEntry(target, odysea::core::WorkingEntryRole::Prepared);
    QVERIFY(!retained.empty());
    QCOMPARE(QString::fromStdString(readFile(retained / "kept.txt")), QStringLiteral("only copy"));

    DirectoryListModel model;
    model.setPath(QString::fromStdString(target.string()));
    QTRY_VERIFY_WITH_TIMEOUT(!model.busy(), 5000);
    QCOMPARE(model.rowCount(), 1);

    model.setShowHidden(true);
    QTRY_COMPARE_WITH_TIMEOUT(model.rowCount(), 2, 1000);
    const int retainedRow = rowForName(model, QString::fromStdString(retained.filename().string()));
    QVERIFY(retainedRow >= 0);
    QCOMPARE(model.data(model.index(retainedRow), DirectoryListModel::RecoveryEntryRole).toBool(),
             true);

    model.setOperationBusy(true);
    model.applyPresentationSettings();
    QCOMPARE(model.rowCount(), 2);
    QVERIFY(rowForName(model, QString::fromStdString(retained.filename().string())) >= 0);
    QVERIFY(fs::exists(retained / "kept.txt"));
}

void DirectoryListModelTest::overflowRequestsARescan() {
    QTemporaryDir fixture;
    QVERIFY(fixture.isValid());
    const fs::path root = fixture.path().toStdString();

    DirectoryListModel model;
    model.setPath(fixture.path());
    QTRY_VERIFY_WITH_TIMEOUT(!model.busy(), 5000);
    writeFile(root / "after-overflow.txt");

    model.applyWatchUpdate(DirectoryWatchUpdate{.token = model.watchToken_,
                                                .directory = root,
                                                .removedNames = {},
                                                .updatedEntries = {},
                                                .renamedEntries = {},
                                                .error = {},
                                                .rescanRequired = true});
    QTRY_VERIFY_WITH_TIMEOUT(!model.busy(), 5000);
    QVERIFY(rowForName(model, QStringLiteral("after-overflow.txt")) >= 0);
}

void DirectoryListModelTest::destructionDropsQueuedWorkerCallbacks() {
    QTemporaryDir fixture;
    QVERIFY(fixture.isValid());
    const fs::path root = fixture.path().toStdString();
    for (int index = 0; index < 600; ++index) {
        writeFile(root / ("queued-" + std::to_string(index)));
    }

    for (int iteration = 0; iteration < 20; ++iteration) {
        auto model = std::make_unique<DirectoryListModel>();
        model->setPath(fixture.path());
        writeFile(root / ("change-" + std::to_string(iteration)));
        QCoreApplication::processEvents();
        model.reset();
        QCoreApplication::processEvents();
    }

    const fs::path destination = root / "destination";
    fs::create_directories(destination);
    auto model = std::make_unique<DirectoryListModel>();
    model->setPath(fixture.path());
    QTRY_VERIFY_WITH_TIMEOUT(!model->busy(), 5000);
    const int sourceRow = rowForName(*model, QStringLiteral("queued-0"));
    QVERIFY(sourceRow >= 0);
    model->selectRow(sourceRow, Qt::NoModifier);
    model->performCopy(QString::fromStdString(destination.string()),
                       DirectoryListModel::ConflictFail);
    model.reset();
    QVERIFY(QThreadPool::globalInstance()->waitForDone(5000));
    QCoreApplication::processEvents();
    QVERIFY(true);
}

QTEST_GUILESS_MAIN(DirectoryListModelTest)

#include "tst_directory_list_model.moc"
