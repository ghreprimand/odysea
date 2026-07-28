#include "directory_list_model.hpp"

#include "file_operations_internal.hpp"

#include <QByteArray>
#include <QCoreApplication>
#include <QTemporaryDir>
#include <QTest>
#include <QThreadPool>

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

} // namespace

class DirectoryListModelTest : public QObject {
    Q_OBJECT

  private slots:
    void rapidNavigationCancelsStaleBatches();
    void incrementalScannerPublishesBatches();
    void watcherBurstPreservesRenamedSelection();
    void hardLinksRemainDistinctAcrossRefreshAndRename();
    void uniqueInodeFallbackPreservesRename();
    void selectionSurvivesSortFilterAndRefresh();
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
    connect(&model, &QAbstractItemModel::modelReset, &model, [&] {
        if (model.busy() && model.rowCount() > 0 && model.rowCount() < entryCount) {
            sawIncrementalBatch = true;
        }
    });
    model.setPath(fixture.path());

    QTRY_VERIFY_WITH_TIMEOUT(sawIncrementalBatch, 5000);
    QTRY_VERIFY_WITH_TIMEOUT(!model.busy(), 5000);
    QCOMPARE(model.rowCount(), entryCount);
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
