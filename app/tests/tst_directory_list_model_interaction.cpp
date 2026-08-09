// Headless tests for what the directory model does when a person drives it:
// selection and cursor contracts, direct path entry and completion,
// activation and breadcrumbs, drop acceptance, and filesystem operations.
//
// How a listing is acquired and kept consistent lives in
// tst_directory_list_model.cpp. The split follows that boundary rather than
// file size.
#include "directory_list_model_test_support.hpp"

#include <QCoreApplication>
#include <QDir>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QUrl>
#include <QVariantList>
#include <QVariantMap>

#include <algorithm>
#include <memory>

namespace fs = std::filesystem;
using namespace odysea::apptest;

class DirectoryListModelInteractionTest : public QObject {
    Q_OBJECT

  private slots:
    void selectionSurvivesSortFilterAndRefresh();
    void explicitGeometricSelectionUsesRowSet();
    void cursorMovementAndPrefixSearchPreserveSelectionContracts();
    void sameParentCopyCreatesSiblingDuplicate();
    void directDirectoryActivationNavigates();
    void fuzzyResultNavigationRevealsFilesAndEntersDirectories();
    void navigationInputResolvesTilde();
    void directPathInputValidatesDirectory();
    void invalidDirectPathInputDoesNotNavigate();
    void sharedPathCompletionFinishesTheNextSegmentPrefix();
    void uniquePathCompletionFinishesTheDirectoryName();
    void activationBreadcrumbsAndDropContracts();
    void symlinkTargetDirectoryChangesRefreshRole();
    void operationsReachCoreAndReportFailures();
    void retainedRecoveryRemainsVisibleDuringOperation();
};
void DirectoryListModelInteractionTest::selectionSurvivesSortFilterAndRefresh() {
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
void DirectoryListModelInteractionTest::explicitGeometricSelectionUsesRowSet() {
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
void DirectoryListModelInteractionTest::cursorMovementAndPrefixSearchPreserveSelectionContracts() {
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

// QTRY_VERIFY_WITH_TIMEOUT expands into the branches reported here.
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
void DirectoryListModelInteractionTest::fuzzyResultNavigationRevealsFilesAndEntersDirectories() {
    QTemporaryDir fixture;
    QVERIFY(fixture.isValid());
    const fs::path root = fixture.path().toStdString();
    const fs::path nested = root / "nested";
    const fs::path document = nested / "target.txt";
    fs::create_directories(nested / "child");
    writeFile(document);
    constexpr int decoyCount = 512;
    for (int index = 0; index < decoyCount; ++index) {
        writeFile(nested / ("decoy-" + std::to_string(index) + ".txt"));
    }

    DirectoryListModel model;
    model.setPath(fixture.path());
    QTRY_VERIFY_WITH_TIMEOUT(!model.busy(), 5000);
    model.setFilterText(QStringLiteral("does-not-match"));

    const quint64 keyBuildsBefore = model.entryKeyBuilds_;
    const quint64 rowIndexBuildsBefore = model.entryRowIndexBuilds_;
    model.navigateToEntry(QString::fromStdString(document.string()), false);
    QTRY_VERIFY_WITH_TIMEOUT(!model.busy(), 5000);
    QCOMPARE(QDir::cleanPath(model.path()),
             QDir::cleanPath(QString::fromStdString(nested.string())));
    QCOMPARE(model.filterText(), QString());
    QCOMPARE(model.selectedCount(), 1);
    QCOMPARE(selectedName(model), QStringLiteral("target.txt"));
    QCOMPARE(currentName(model), QStringLiteral("target.txt"));
    constexpr auto listingRows = static_cast<quint64>(decoyCount) + 2ULL;
    const quint64 keyBuilds = model.entryKeyBuilds_ - keyBuildsBefore;
    const quint64 rowIndexBuilds = model.entryRowIndexBuilds_ - rowIndexBuildsBefore;
    qInfo("fuzzy reveal of %llu rows built %llu keys and %llu row indexes",
          static_cast<unsigned long long>(listingRows), static_cast<unsigned long long>(keyBuilds),
          static_cast<unsigned long long>(rowIndexBuilds));
    QVERIFY(keyBuilds >= listingRows);
    QVERIFY(keyBuilds <= listingRows * 8);
    QVERIFY(rowIndexBuilds >= 1);
    QVERIFY(rowIndexBuilds <= 16);

    model.navigateToEntry(QString::fromStdString((nested / "child").string()), true);
    QTRY_VERIFY_WITH_TIMEOUT(!model.busy(), 5000);
    QCOMPARE(QDir::cleanPath(model.path()),
             QDir::cleanPath(QString::fromStdString((nested / "child").string())));
}
void DirectoryListModelInteractionTest::sameParentCopyCreatesSiblingDuplicate() {
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
void DirectoryListModelInteractionTest::directDirectoryActivationNavigates() {
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
void DirectoryListModelInteractionTest::navigationInputResolvesTilde() {
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
void DirectoryListModelInteractionTest::directPathInputValidatesDirectory() {
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
void DirectoryListModelInteractionTest::invalidDirectPathInputDoesNotNavigate() {
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
void DirectoryListModelInteractionTest::sharedPathCompletionFinishesTheNextSegmentPrefix() {
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
void DirectoryListModelInteractionTest::uniquePathCompletionFinishesTheDirectoryName() {
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
void DirectoryListModelInteractionTest::activationBreadcrumbsAndDropContracts() {
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
void DirectoryListModelInteractionTest::symlinkTargetDirectoryChangesRefreshRole() {
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
void DirectoryListModelInteractionTest::operationsReachCoreAndReportFailures() {
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
void DirectoryListModelInteractionTest::retainedRecoveryRemainsVisibleDuringOperation() {
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

QTEST_GUILESS_MAIN(DirectoryListModelInteractionTest)

#include "tst_directory_list_model_interaction.moc"
