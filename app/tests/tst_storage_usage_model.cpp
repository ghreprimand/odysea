// App-layer coverage for the Qt bridge over the core storage-usage scanner.
#include "storage_usage_model.hpp"

#include <QAbstractItemModel>
#include <QDir>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>

namespace fs = std::filesystem;
using odysea::app::StorageUsageModel;

namespace {

void writeFile(const fs::path& path, std::string_view contents = "synthetic usage") {
    std::ofstream stream(path, std::ios::binary);
    stream << contents;
}

int rowForName(const StorageUsageModel& model, const QString& name) {
    for (int row = 0; row < model.rowCount(); ++row) {
        if (model.data(model.index(row), StorageUsageModel::NameRole).toString() == name) {
            return row;
        }
    }
    return -1;
}

// QTest retry macros expand into loops that inflate this helper's score.
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
void waitForIdle(StorageUsageModel& model) {
    QTRY_VERIFY_WITH_TIMEOUT(!model.busy(), 10000);
}

} // namespace

class StorageUsageModelTest : public QObject {
    Q_OBJECT

  private slots:
    void scanPublishesAccountingAndRoles();
    void selectionActivationAndUpNavigateOneModel();
    void cancellationAndSupersessionAreObservable();
    void progressChangesAtMostOneExistingRowPerDelivery();
};

// QTest assertions inflate the reported score without adding control flow here.
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
void StorageUsageModelTest::scanPublishesAccountingAndRoles() {
    QTemporaryDir fixture;
    QVERIFY(fixture.isValid());
    const fs::path root = fixture.path().toStdString();
    fs::create_directories(root / "alpha");
    fs::create_directories(root / "beta");
    writeFile(root / "alpha" / "payload.bin", std::string(4096, 'a'));

    std::error_code linkError;
    fs::create_hard_link(root / "alpha" / "payload.bin", root / "beta" / "repeat.bin", linkError);
    QVERIFY2(!linkError, linkError.message().c_str());

    StorageUsageModel model;
    QSignalSpy completed(&model, &StorageUsageModel::scanCompleted);
    model.start(fixture.path());
    waitForIdle(model);

    QCOMPARE(completed.count(), 1);
    QCOMPARE(model.rowCount(), 2);
    QCOMPARE(model.deduplicatedEntries(), 1ULL);
    QVERIFY(model.entriesVisited() >= 5);
    QVERIFY(model.allocatedBytes() > 0);
    QVERIFY(model.apparentBytes() > 0);
    QVERIFY(model.errorString().isEmpty());
    QVERIFY(!model.cancelled());

    const auto roles = model.roleNames();
    QCOMPARE(roles.value(StorageUsageModel::NameRole), QByteArray("name"));
    QCOMPARE(roles.value(StorageUsageModel::AllocatedTextRole), QByteArray("allocatedText"));
    QCOMPARE(roles.value(StorageUsageModel::SelectedRole), QByteArray("selected"));

    const int alpha = rowForName(model, QStringLiteral("alpha"));
    QVERIFY(alpha >= 0);
    const QModelIndex index = model.index(alpha);
    QVERIFY(model.data(index, StorageUsageModel::IsDirectoryRole).toBool());
    QCOMPARE(model.data(index, StorageUsageModel::KindLabelRole).toString(),
             QStringLiteral("folder"));
    QVERIFY(!model.data(index, StorageUsageModel::AllocatedTextRole).toString().isEmpty());
    QVERIFY(model.flags(index).testFlag(Qt::ItemIsSelectable));
    QCOMPARE(StorageUsageModel::formatBytes(1536), QStringLiteral("1.5 KiB"));
}

// QTest retry macros expand into loops that inflate this integration test's score.
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
void StorageUsageModelTest::selectionActivationAndUpNavigateOneModel() {
    QTemporaryDir fixture;
    QVERIFY(fixture.isValid());
    const fs::path root = fixture.path().toStdString();
    fs::create_directories(root / "subtree");
    writeFile(root / "subtree" / "inside.txt");
    writeFile(root / "outside.txt");

    StorageUsageModel model;
    model.start(fixture.path());
    waitForIdle(model);

    const int subtree = rowForName(model, QStringLiteral("subtree"));
    QVERIFY(subtree >= 0);
    model.selectRow(subtree);
    QCOMPARE(model.currentIndex(), subtree);
    QVERIFY(model.rowSelected(subtree));
    QVERIFY(model.data(model.index(subtree), StorageUsageModel::SelectedRole).toBool());

    model.activateCurrent();
    waitForIdle(model);
    QCOMPARE(QDir::cleanPath(model.rootPath()),
             QDir::cleanPath(QString::fromStdString((root / "subtree").string())));
    QVERIFY(model.canGoUp());
    QCOMPARE(model.rowCount(), 1);

    model.goUp();
    waitForIdle(model);
    QCOMPARE(QDir::cleanPath(model.rootPath()), QDir::cleanPath(fixture.path()));
    model.moveCursor(1);
    QVERIFY(model.currentIndex() >= 0);
}

// QTest retry macros expand into loops that inflate this integration test's score.
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
void StorageUsageModelTest::cancellationAndSupersessionAreObservable() {
    QTemporaryDir firstFixture;
    QTemporaryDir secondFixture;
    QVERIFY(firstFixture.isValid());
    QVERIFY(secondFixture.isValid());

    const fs::path firstRoot = firstFixture.path().toStdString();
    for (int entry = 0; entry < 3000; ++entry) {
        writeFile(firstRoot / ("entry-" + std::to_string(entry)), "x");
    }
    writeFile(fs::path(secondFixture.path().toStdString()) / "destination.txt", "done");

    StorageUsageModel model;
    QSignalSpy cancelled(&model, &StorageUsageModel::scanCancelled);
    model.start(firstFixture.path());
    model.start(secondFixture.path());
    waitForIdle(model);

    QTRY_VERIFY_WITH_TIMEOUT(cancelled.count() >= 1, 5000);
    QCOMPARE(QDir::cleanPath(cancelled.at(0).at(0).toString()),
             QDir::cleanPath(firstFixture.path()));
    QCOMPARE(QDir::cleanPath(model.rootPath()), QDir::cleanPath(secondFixture.path()));
    QCOMPARE(model.rowCount(), 1);
    QVERIFY(!model.cancelled());

    model.start(firstFixture.path());
    QVERIFY(model.busy());
    model.cancel();
    QVERIFY(model.cancelling());
    waitForIdle(model);
    QVERIFY(model.cancelled());
    QVERIFY(!model.cancelling());
    QVERIFY(model.entriesVisited() < 3001ULL);
    QVERIFY(cancelled.count() >= 2);
}

void StorageUsageModelTest::progressChangesAtMostOneExistingRowPerDelivery() {
    QTemporaryDir fixture;
    QVERIFY(fixture.isValid());
    const fs::path root = fixture.path().toStdString();
    for (int directory = 0; directory < 3; ++directory) {
        const fs::path child = root / ("branch-" + std::to_string(directory));
        fs::create_directories(child);
        for (int entry = 0; entry < 600; ++entry) {
            writeFile(child / ("leaf-" + std::to_string(entry)), "x");
        }
    }

    StorageUsageModel model;
    QSignalSpy changes(&model, &QAbstractItemModel::dataChanged);
    model.start(fixture.path());
    waitForIdle(model);

    QVERIFY(changes.count() > 0);
    for (const QList<QVariant>& arguments : changes) {
        const auto topLeft = qvariant_cast<QModelIndex>(arguments.at(0));
        const auto bottomRight = qvariant_cast<QModelIndex>(arguments.at(1));
        QCOMPARE(topLeft.row(), bottomRight.row());
    }
    QCOMPARE(model.rowCount(), 3);
}

QTEST_GUILESS_MAIN(StorageUsageModelTest)

#include "tst_storage_usage_model.moc"
