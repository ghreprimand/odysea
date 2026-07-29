#include "directory_list_model.hpp"

#include "entry_launcher.hpp"
#include "file_operations_internal.hpp"

#include <QByteArray>
#include <QCoreApplication>
#include <QPersistentModelIndex>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QThreadPool>
#include <QUrl>

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

  private slots:
    void rapidNavigationCancelsStaleBatches();
    void incrementalScannerPublishesBatches();
    void watchAndPresentationUpdatesUseGranularSignals();
    void navigationHistorySurvivesIncrementalUpdates();
    void watcherBurstPreservesRenamedSelection();
    void hardLinksRemainDistinctAcrossRefreshAndRename();
    void uniqueInodeFallbackPreservesRename();
    void selectionSurvivesSortFilterAndRefresh();
    void explicitGeometricSelectionUsesRowSet();
    void cursorMovementAndPrefixSearchPreserveSelectionContracts();
    void activationBreadcrumbsAndDropContracts();
    void symlinkTargetDirectoryChangesRefreshRole();
    void operationsReachCoreAndReportFailures();
    void retainedRecoveryRemainsVisibleDuringOperation();
    void overflowRequestsARescan();
    void destructionDropsQueuedWorkerCallbacks();
};

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

    QVERIFY(!model.canDropSelection(QString::fromStdString(source.string())));
    QVERIFY(model.canDropSelection(QString::fromStdString(destination.string())));
    QVERIFY(model.dropSelection(QString::fromStdString(destination.string()), false,
                                DirectoryListModel::ConflictFail));
    QTRY_VERIFY_WITH_TIMEOUT(!model.operationBusy(), 5000);
    QVERIFY(fs::exists(destination / "document.txt"));

    QTRY_VERIFY_WITH_TIMEOUT(rowForName(model, QStringLiteral("folder-link")) >= 0, 5000);
    const int remoteLinkRow = rowForName(model, QStringLiteral("remote-link"));
    QVERIFY(remoteLinkRow >= 0);
    model.selectRow(remoteLinkRow, Qt::NoModifier);
    QVERIFY(!model.canDropSelection(QString::fromStdString(source.string())));

    const int folderLinkRow = rowForName(model, QStringLiteral("folder-link"));
    QVERIFY(model.rowIsDirectory(folderLinkRow));
    QVERIFY(model.data(model.index(folderLinkRow), DirectoryListModel::IsDirRole).toBool());
    model.selectRow(documentRow, Qt::NoModifier);
    QVERIFY(model.canDropSelection(QString::fromStdString(folderLink.string())));
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
    QVERIFY(!model.canDropSelection(QString::fromStdString(folder.string())));
    QVERIFY(!model.canDropSelection(QString::fromStdString(descendant.string())));
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
    QVERIFY(!model.canDropSelection(QString::fromStdString(remoteLink.string())));
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
