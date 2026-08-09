#include "fuzzy_find_model.hpp"

#include <QDebug>
#include <QElapsedTimer>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <string>

namespace fs = std::filesystem;
using odysea::app::FuzzyFindModel;

namespace {

void writeFile(const fs::path& path) {
    std::ofstream stream(path);
    stream << "x";
}

int rowForName(const FuzzyFindModel& model, const QString& name) {
    for (int row = 0; row < model.rowCount(); ++row) {
        if (model.data(model.index(row), FuzzyFindModel::NameRole).toString() == name) {
            return row;
        }
    }
    return -1;
}

} // namespace

class FuzzyFindModelTest : public QObject {
    Q_OBJECT

  private slots:
    void scanAndRepeatedQueriesReuseOneFilesystemWalk();
    void rolesSelectionAndActivationDescribeTheRankedResult();
    void hiddenPolicyIsAppliedWhenTheCorpusIsBuilt();
    void cancellationImmediatelyLeavesTheExposedModelIdle();
};

// QtTest assertion and retry macros deliberately expand to do-while loops.
// NOLINTBEGIN(cppcoreguidelines-avoid-do-while)
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
void FuzzyFindModelTest::scanAndRepeatedQueriesReuseOneFilesystemWalk() {
    QTemporaryDir fixture;
    QVERIFY(fixture.isValid());
    const fs::path root = fixture.path().toStdString();
    constexpr int directoryCount = 32;
    constexpr int filesPerDirectory = 256;
    for (int directory = 0; directory < directoryCount; ++directory) {
        const fs::path branch = root / ("branch-" + std::to_string(directory));
        fs::create_directory(branch);
        for (int file = 0; file < filesPerDirectory; ++file) {
            writeFile(branch / ("component-file-" + std::to_string(file) + ".cpp"));
        }
    }

    FuzzyFindModel model;
    model.start(fixture.path(), false);
    QTRY_VERIFY_WITH_TIMEOUT(!model.busy(), 10000);
    const qulonglong indexed =
        static_cast<qulonglong>(directoryCount) * static_cast<qulonglong>(filesPerDirectory + 1);
    QCOMPARE(model.candidatesIndexed(), indexed);
    QCOMPARE(model.entriesVisited(), indexed);
    QCOMPARE(model.directoriesVisited(), static_cast<qulonglong>(directoryCount + 1));
    QCOMPARE(model.filesystemWalks(), 1ULL);

    const QStringList keystrokes{QStringLiteral("c"), QStringLiteral("co"), QStringLiteral("com"),
                                 QStringLiteral("comp"), QStringLiteral("component")};
    qint64 maximumKeystrokeMs = 0;
    QSignalSpy stateChanged(&model, &FuzzyFindModel::stateChanged);
    for (const QString& query : keystrokes) {
        stateChanged.clear();
        QElapsedTimer elapsed;
        elapsed.start();
        model.setQuery(query);
        while (model.ranking()) {
            QVERIFY2(stateChanged.wait(2000), "the rank request did not complete");
        }
        maximumKeystrokeMs = std::max(maximumKeystrokeMs, elapsed.elapsed());
        QVERIFY2(elapsed.elapsed() < 500, "one cached-corpus keystroke exceeded 500 ms");
        QCOMPARE(model.filesystemWalks(), 1ULL);
        QCOMPARE(model.candidatesExamined(), indexed);
        QVERIFY(model.characterComparisons() >= indexed);
        QVERIFY(model.characterComparisons() <= indexed * 80);
    }
    QCOMPARE(model.rankRequests(), static_cast<qulonglong>(keystrokes.size()));
    qInfo().noquote() << "fuzzy_find_model:" << indexed << "indexed paths, 5 keystrokes, max"
                      << maximumKeystrokeMs << "ms, 1 filesystem walk";
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
void FuzzyFindModelTest::rolesSelectionAndActivationDescribeTheRankedResult() {
    QTemporaryDir fixture;
    QVERIFY(fixture.isValid());
    const fs::path root = fixture.path().toStdString();
    fs::create_directory(root / "manuals");
    writeFile(root / "manuals" / "voyager-guide.txt");
    writeFile(root / "notes.txt");

    FuzzyFindModel model;
    model.start(fixture.path(), false);
    QTRY_VERIFY_WITH_TIMEOUT(!model.busy(), 5000);
    model.setQuery(QStringLiteral("voyguide"));
    QTRY_VERIFY_WITH_TIMEOUT(!model.ranking(), 5000);
    QCOMPARE(model.rowCount(), 1);
    QCOMPARE(model.data(model.index(0), FuzzyFindModel::NameRole).toString(),
             QStringLiteral("voyager-guide.txt"));
    QCOMPARE(model.data(model.index(0), FuzzyFindModel::RelativePathRole).toString(),
             QStringLiteral("manuals/voyager-guide.txt"));
    QVERIFY(!model.data(model.index(0), FuzzyFindModel::IsDirectoryRole).toBool());
    QVERIFY(model.rowSelected(0));

    QSignalSpy activated(&model, &FuzzyFindModel::resultActivated);
    model.activateCurrent();
    QCOMPARE(activated.count(), 1);
    QCOMPARE(
        QDir::cleanPath(activated.at(0).at(0).toString()),
        QDir::cleanPath(QString::fromStdString((root / "manuals" / "voyager-guide.txt").string())));
    QVERIFY(!activated.at(0).at(1).toBool());

    model.setQuery(QStringLiteral("manuals"));
    QTRY_VERIFY_WITH_TIMEOUT(!model.ranking(), 5000);
    const int directoryRow = rowForName(model, QStringLiteral("manuals"));
    QVERIFY(directoryRow >= 0);
    QVERIFY(model.data(model.index(directoryRow), FuzzyFindModel::IsDirectoryRole).toBool());
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
void FuzzyFindModelTest::hiddenPolicyIsAppliedWhenTheCorpusIsBuilt() {
    QTemporaryDir fixture;
    QVERIFY(fixture.isValid());
    const fs::path root = fixture.path().toStdString();
    fs::create_directory(root / ".private");
    writeFile(root / ".private" / "secret.txt");

    FuzzyFindModel model;
    model.start(fixture.path(), false);
    QTRY_VERIFY_WITH_TIMEOUT(!model.busy(), 5000);
    QCOMPARE(model.candidatesIndexed(), 0ULL);

    model.start(fixture.path(), true);
    QTRY_VERIFY_WITH_TIMEOUT(!model.busy(), 5000);
    QCOMPARE(model.candidatesIndexed(), 2ULL);
    QCOMPARE(model.filesystemWalks(), 2ULL);
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
void FuzzyFindModelTest::cancellationImmediatelyLeavesTheExposedModelIdle() {
    QTemporaryDir fixture;
    QVERIFY(fixture.isValid());

    FuzzyFindModel model;
    model.start(fixture.path(), false);
    QVERIFY(model.busy());
    model.cancel();
    QVERIFY(!model.busy());
    QVERIFY(!model.ranking());
}
// NOLINTEND(cppcoreguidelines-avoid-do-while)

QTEST_GUILESS_MAIN(FuzzyFindModelTest)

#include "tst_fuzzy_find_model.moc"
