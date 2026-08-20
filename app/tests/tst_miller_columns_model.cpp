#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QPointer>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>

#include <algorithm>
#include <array>
#include <filesystem>
#include <system_error>

#include "directory_list_model.hpp"
#include "entry_launcher.hpp"
#include "miller_columns_model.hpp"

namespace {

class RecordingLauncher final : public EntryLauncher {
  public:
    bool open(const std::filesystem::path& path, std::error_code& error) override {
        error.clear();
        openedPath = path;
        ++openCount;
        return true;
    }

    std::filesystem::path openedPath;
    int openCount = 0;
};

void createFile(const QString& path) {
    QFile file(path);
    QVERIFY2(file.open(QIODevice::WriteOnly), qPrintable(file.errorString()));
    QCOMPARE(file.write("fixture"), 7);
}

DirectoryListModel* listing(odysea::app::MillerColumnsModel& model, int column) {
    return qobject_cast<DirectoryListModel*>(model.columnModel(column));
}

// Qt's wait macros expand into several branches around the two assertions.
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
void waitForListing(odysea::app::MillerColumnsModel& model, int column) {
    QTRY_VERIFY_WITH_TIMEOUT(listing(model, column) != nullptr, 5000);
    QTRY_VERIFY_WITH_TIMEOUT(!listing(model, column)->busy(), 5000);
}

int rowForName(const odysea::app::MillerColumnsModel& model, int column, const QString& name) {
    for (int row = 0; row < model.entryCount(column); ++row) {
        if (model.entryName(column, row) == name) {
            return row;
        }
    }
    return -1;
}

} // namespace

class MillerColumnsModelTest : public QObject {
    Q_OBJECT

  private slots:
    void selectionBuildsOnlyTheLivePathChain();
    void reselectingAnOpenBranchReusesItsListing();
    void traversalAndActivationCoverFoldersAndFiles();
    void focusedColumnPublishesLocationTransitions();
    void presentationSettingsPreserveThePathChain();
    void fiveLargeColumnsBoundRetainedState();
};

// Qt's assertion macros account for the reported branch count.
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
void MillerColumnsModelTest::selectionBuildsOnlyTheLivePathChain() {
    QTemporaryDir fixture;
    QVERIFY(fixture.isValid());
    QVERIFY(QDir().mkpath(fixture.path() + QStringLiteral("/alpha/deep")));
    QVERIFY(QDir().mkpath(fixture.path() + QStringLiteral("/beta")));
    createFile(fixture.path() + QStringLiteral("/alpha/deep/item.txt"));

    RecordingLauncher launcher;
    odysea::app::MillerColumnsModel model(launcher);
    model.setRootPath(fixture.path());
    waitForListing(model, 0);

    const int alpha = rowForName(model, 0, QStringLiteral("alpha"));
    const int beta = rowForName(model, 0, QStringLiteral("beta"));
    QVERIFY(alpha >= 0);
    QVERIFY(beta >= 0);

    model.select(0, alpha);
    QCOMPARE(model.liveColumnCount(), 2);
    waitForListing(model, 1);
    QPointer<DirectoryListModel> abandonedBranch = listing(model, 1);

    const int deep = rowForName(model, 1, QStringLiteral("deep"));
    QVERIFY(deep >= 0);
    model.select(1, deep);
    QCOMPARE(model.liveColumnCount(), 3);
    waitForListing(model, 2);

    model.select(0, beta);
    QCOMPARE(model.liveColumnCount(), 2);
    QVERIFY(abandonedBranch.isNull());
    QCOMPARE(model.entryPath(0, beta), model.currentPath() + QStringLiteral("/beta"));
    QCOMPARE(listing(model, 1)->path(), fixture.path() + QStringLiteral("/beta"));

    model.collapseBack();
    QCOMPARE(model.liveColumnCount(), 1);
    QCOMPARE(model.activeColumn(), 0);
}

void MillerColumnsModelTest::reselectingAnOpenBranchReusesItsListing() {
    QTemporaryDir fixture;
    QVERIFY(fixture.isValid());
    QVERIFY(QDir().mkpath(fixture.path() + QStringLiteral("/folder")));

    RecordingLauncher launcher;
    odysea::app::MillerColumnsModel model(launcher);
    model.setRootPath(fixture.path());
    waitForListing(model, 0);

    const int folder = rowForName(model, 0, QStringLiteral("folder"));
    QVERIFY(folder >= 0);
    model.select(0, folder);
    waitForListing(model, 1);
    QPointer<DirectoryListModel> child = listing(model, 1);

    model.select(0, folder);

    QCOMPARE(model.liveColumnCount(), 2);
    QVERIFY(!child.isNull());
    QCOMPARE(listing(model, 1), child.data());
}

// Qt's assertion macros account for the reported branch count.
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
void MillerColumnsModelTest::traversalAndActivationCoverFoldersAndFiles() {
    QTemporaryDir fixture;
    QVERIFY(fixture.isValid());
    QVERIFY(QDir().mkpath(fixture.path() + QStringLiteral("/folder")));
    const QString document = fixture.path() + QStringLiteral("/note.txt");
    createFile(document);

    RecordingLauncher launcher;
    odysea::app::MillerColumnsModel model(launcher);
    QSignalSpy activation(&model, &odysea::app::MillerColumnsModel::fileActivated);
    model.setRootPath(fixture.path());
    waitForListing(model, 0);

    const int initial = listing(model, 0)->currentIndex();
    model.moveWithin(1);
    const int moved = initial < 0 ? 0 : std::min(initial + 1, model.entryCount(0) - 1);
    QCOMPARE(listing(model, 0)->currentIndex(), moved);
    QCOMPARE(model.liveColumnCount(), model.entryIsDirectory(0, moved) ? 2 : 1);

    const int folder = rowForName(model, 0, QStringLiteral("folder"));
    QVERIFY(folder >= 0);
    model.activate(0, folder);
    QCOMPARE(model.liveColumnCount(), 2);
    QCOMPARE(model.activeColumn(), 1);
    model.moveAcross(-1);
    QCOMPARE(model.activeColumn(), 0);
    model.moveAcross(1);
    QCOMPARE(model.activeColumn(), 1);

    const int note = rowForName(model, 0, QStringLiteral("note.txt"));
    QVERIFY(note >= 0);
    model.activate(0, note);
    QCOMPARE(model.liveColumnCount(), 1);
    QCOMPARE(launcher.openCount, 1);
    QCOMPARE(QString::fromStdString(launcher.openedPath.string()), document);
    QCOMPARE(activation.count(), 1);
    QCOMPARE(activation.first().first().toString(), document);
}

// Qt's wait macros expand into several branches around the two assertions.
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
void MillerColumnsModelTest::focusedColumnPublishesLocationTransitions() {
    QTemporaryDir fixture;
    QVERIFY(fixture.isValid());
    const QString childPath = fixture.path() + QStringLiteral("/folder");
    QVERIFY(QDir().mkpath(childPath));

    RecordingLauncher launcher;
    odysea::app::MillerColumnsModel model(launcher);
    model.setRootPath(fixture.path());
    waitForListing(model, 0);
    QSignalSpy locationChanged(&model, &odysea::app::MillerColumnsModel::currentPathChanged);

    const int folder = rowForName(model, 0, QStringLiteral("folder"));
    QVERIFY(folder >= 0);
    model.select(0, folder);
    waitForListing(model, 1);
    QCOMPARE(model.currentPath(), fixture.path());
    QCOMPARE(locationChanged.count(), 0);

    model.moveAcross(1);
    QCOMPARE(model.currentPath(), childPath);
    QCOMPARE(locationChanged.count(), 1);

    model.moveAcross(-1);
    QCOMPARE(model.currentPath(), fixture.path());
    QCOMPARE(locationChanged.count(), 2);

    model.moveAcross(1);
    QCOMPARE(model.currentPath(), childPath);
    QCOMPARE(locationChanged.count(), 3);

    model.collapseBack();
    QCOMPARE(model.liveColumnCount(), 1);
    QCOMPARE(model.currentPath(), fixture.path());
    QCOMPARE(locationChanged.count(), 4);
}

// Qt's assertion macros account for the reported branch count.
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
void MillerColumnsModelTest::presentationSettingsPreserveThePathChain() {
    QTemporaryDir fixture;
    QVERIFY(fixture.isValid());
    QVERIFY(QDir().mkpath(fixture.path() + QStringLiteral("/folder")));

    RecordingLauncher launcher;
    odysea::app::MillerColumnsModel model(launcher);
    model.setShowHidden(true);
    model.setSortMode(DirectoryListModel::SortBySize);
    model.setFilterText(QStringLiteral("fold"));
    model.setRootPath(fixture.path());
    waitForListing(model, 0);

    DirectoryListModel* root = listing(model, 0);
    QVERIFY(root->showHidden());
    QCOMPARE(root->sortMode(), static_cast<int>(DirectoryListModel::SortBySize));
    QCOMPARE(root->filterText(), QStringLiteral("fold"));

    model.setFilterText({});
    waitForListing(model, 0);
    const int folder = rowForName(model, 0, QStringLiteral("folder"));
    QVERIFY(folder >= 0);
    model.select(0, folder);
    QCOMPARE(model.liveColumnCount(), 2);
    DirectoryListModel* child = listing(model, 1);
    QVERIFY(child->showHidden());
    QCOMPARE(child->sortMode(), static_cast<int>(DirectoryListModel::SortBySize));
    QCOMPARE(child->filterText(), QString{});

    model.setFilterText(QStringLiteral("folder"));
    QCOMPARE(model.liveColumnCount(), 1);
    QCOMPARE(root->filterText(), QStringLiteral("folder"));

    model.setFilterText({});
    waitForListing(model, 0);
    const int reopenedFolder = rowForName(model, 0, QStringLiteral("folder"));
    QVERIFY(reopenedFolder >= 0);
    model.select(0, reopenedFolder);
    waitForListing(model, 1);
    child = listing(model, 1);
    model.setActiveColumn(1);
    model.setFilterText(QStringLiteral("needle"));
    QCOMPARE(root->filterText(), QString{});
    QCOMPARE(child->filterText(), QStringLiteral("needle"));

    model.setShowHidden(false);
    model.setSortMode(DirectoryListModel::SortByType);
    QVERIFY(!root->showHidden());
    QVERIFY(!child->showHidden());
    QCOMPARE(root->sortMode(), static_cast<int>(DirectoryListModel::SortByType));
    QCOMPARE(child->sortMode(), static_cast<int>(DirectoryListModel::SortByType));
}

// Qt's assertion macros account for the reported branch count.
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
void MillerColumnsModelTest::fiveLargeColumnsBoundRetainedState() {
    constexpr int depth = 5;
    constexpr int filesPerLevel = 1000;

    QTemporaryDir fixture;
    QVERIFY(fixture.isValid());

    QString levelPath = fixture.path();
    for (int level = 0; level < depth; ++level) {
        if (level + 1 < depth) {
            QVERIFY(QDir().mkpath(levelPath + QStringLiteral("/child")));
        }
        for (int row = 0; row < filesPerLevel; ++row) {
            createFile(levelPath +
                       QStringLiteral("/entry-%1.txt").arg(row, 4, 10, QLatin1Char('0')));
        }
        levelPath += QStringLiteral("/child");
    }

    RecordingLauncher launcher;
    odysea::app::MillerColumnsModel model(launcher);
    std::array<QPointer<DirectoryListModel>, depth> liveListings;
    int retainedRows = 0;

    QElapsedTimer timer;
    timer.start();
    model.setRootPath(fixture.path());
    for (int column = 0; column < depth; ++column) {
        waitForListing(model, column);
        liveListings.at(static_cast<std::size_t>(column)) = listing(model, column);
        retainedRows += model.entryCount(column);
        QCOMPARE(model.entryCount(column), filesPerLevel + (column + 1 < depth ? 1 : 0));
        if (column + 1 < depth) {
            const int child = rowForName(model, column, QStringLiteral("child"));
            QVERIFY(child >= 0);
            model.select(column, child);
        }
    }
    const qint64 elapsedMilliseconds = timer.elapsed();

    QCOMPARE(model.liveColumnCount(), depth);
    QCOMPARE(retainedRows, (depth * filesPerLevel) + depth - 1);
    model.setActiveColumn(depth - 1);
    std::uint64_t keyBuildsBefore = 0;
    std::uint64_t rowIndexBuildsBefore = 0;
    for (const auto& listingPointer : liveListings) {
        keyBuildsBefore += listingPointer->entryKeyBuilds_;
        rowIndexBuildsBefore += listingPointer->entryRowIndexBuilds_;
    }

    model.setFilterText(QStringLiteral("entry-"));

    std::uint64_t keyBuildsAfter = 0;
    std::uint64_t rowIndexBuildsAfter = 0;
    for (const auto& listingPointer : liveListings) {
        keyBuildsAfter += listingPointer->entryKeyBuilds_;
        rowIndexBuildsAfter += listingPointer->entryRowIndexBuilds_;
    }
    const std::uint64_t keyBuilds = keyBuildsAfter - keyBuildsBefore;
    const std::uint64_t rowIndexBuilds = rowIndexBuildsAfter - rowIndexBuildsBefore;
    const auto activeRows = static_cast<std::uint64_t>(filesPerLevel);
    QVERIFY2(keyBuilds >= activeRows,
             qPrintable(QStringLiteral("filter built only %1 keys for %2 active rows")
                            .arg(keyBuilds)
                            .arg(activeRows)));
    QVERIFY2(keyBuilds <= activeRows + 8,
             qPrintable(QStringLiteral("filter built %1 keys for %2 active rows")
                            .arg(keyBuilds)
                            .arg(activeRows)));
    QVERIFY2(rowIndexBuilds >= 1, qPrintable(QStringLiteral("filter rebuilt no row index")));
    QVERIFY2(rowIndexBuilds <= 2,
             qPrintable(QStringLiteral("filter rebuilt %1 row indexes").arg(rowIndexBuilds)));
    qInfo().nospace() << depth << " live columns retained " << retainedRows
                      << " rows and loaded in " << elapsedMilliseconds << " ms; filter built "
                      << keyBuilds << " keys and " << rowIndexBuilds << " row indexes";

    model.collapseTo(0);
    QCOMPARE(model.liveColumnCount(), 1);
    QVERIFY(!liveListings.front().isNull());
    for (std::size_t column = 1; column < liveListings.size(); ++column) {
        QVERIFY(liveListings.at(column).isNull());
    }
}

QTEST_GUILESS_MAIN(MillerColumnsModelTest)

#include "tst_miller_columns_model.moc"
