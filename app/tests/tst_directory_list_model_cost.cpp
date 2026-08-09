// Headless tests for what acquiring and reconciling a listing is allowed to
// cost: how the work grows with the directory, how much of a listing may be
// held unpublished and for how long, and how often a rule that publishes on
// elapsed time may fire.
//
// What the model does while acquiring a listing lives in
// tst_directory_list_model.cpp, and what it does when a person drives it in
// tst_directory_list_model_interaction.cpp. These cases are separate because
// they assert a different kind of thing — a bound rather than a behavior —
// and because they are the expensive ones: each builds directories of
// thousands of entries and loads them several times over.
//
// Every bound here is stated with the readings that justify it, healthy and
// defective, at the case that carries it. A ceiling without the measurements
// it sits between is a number nobody can maintain.
#include "directory_list_model_test_support.hpp"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QSignalSpy>
#include <QTemporaryDir>

#include <algorithm>

namespace fs = std::filesystem;
using namespace odysea::apptest;

class DirectoryListModelCostTest : public QObject {
    Q_OBJECT

    /// One load-and-refresh cycle's measurements.
    struct LoadMeasurement {
        qint64 firstScanMilliseconds = 0;
        qint64 refreshMilliseconds = 0;
        quint64 keyBuilds = 0;
    };

    static QString measureDirectoryLoad(int entryCount, SettleBudget settleBudget,
                                        LoadMeasurement& measurement);
    // The cheapest of several loads of the same size. Reported wall clock is
    // the work plus whatever else the machine was doing, and that addition is
    // one-sided, so the smallest reading is the one closest to the cost being
    // bounded.
    static QString measureCheapestDirectoryLoad(int entryCount, SettleBudget settleBudget,
                                                int attempts, LoadMeasurement& measurement);
    // How long this process needs to build the given number of row keys.
    // Machine speed, compiler, allocator, and sanitizer instrumentation all
    // apply to this the same way they apply to a load, so a load measured
    // against it is a cost in keys rather than in milliseconds.
    static qint64 timeKeyConstructions(quint64 count);

  private slots:
    void largeDirectoryLoadStaysWithinBudget();
    void filteringCostsTheSameScatteredAsContiguous();
    void watchDeliveryCostsOneLookupPerDeliveredName();
    void heldEntriesAppearOnceTheyHaveWaitedAsLongAsTheScanHasRun();
    void publishingOnTimeAloneStaysLogarithmicInScanDuration();
};
qint64 DirectoryListModelCostTest::timeKeyConstructions(quint64 count) {
    // The same formula the model's key builder uses, spelled out here rather
    // than called through the model so that timing it cannot itself disturb
    // the model's counter. The paths are synthetic and never touched on disk:
    // normalizing a path is a string operation, and reaching the filesystem
    // would measure the disk instead of the work.
    const std::string base = "/odysea/calibration/entry-";
    QElapsedTimer timer;
    timer.start();
    std::size_t consumed = 0;
    for (quint64 index = 0; index < count; ++index) {
        const std::string spelled = base + std::to_string(index);
        const QString key = QString::fromStdString(fs::path(spelled).lexically_normal().string());
        consumed += static_cast<std::size_t>(key.size());
    }
    const qint64 elapsed = timer.elapsed();
    // Consumed so the loop cannot be optimized away, which would time
    // nothing and make every load look arbitrarily expensive against it.
    return consumed == 0 ? -1 : elapsed;
}
QString DirectoryListModelCostTest::measureDirectoryLoad(int entryCount, SettleBudget settleBudget,
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

    QString mismatch = ModelProbe::rowKeyIndexMismatch(model);
    if (!mismatch.isEmpty()) {
        return mismatch;
    }
    measurement.keyBuilds = model.entryKeyBuilds_;
    return {};
}
QString DirectoryListModelCostTest::measureCheapestDirectoryLoad(int entryCount,
                                                                 SettleBudget settleBudget,
                                                                 int attempts,
                                                                 LoadMeasurement& measurement) {
    // A single reading of a load this size is not stable enough to divide by.
    // Elapsed time is the work plus every delay the machine imposed on it,
    // and delay only ever adds, so repeated readings scatter upwards from the
    // cost rather than around it. Eight consecutive release runs of the ratio
    // below read 2.04 to 2.31 seven times and 3.36 once, against a ceiling of
    // 2.80 that cannot be raised much without reaching the readings a change
    // of exponent produces. Taking the cheapest attempt removes the upward
    // scatter without loosening the bound, which is the property that makes
    // the ratio worth asserting at all.
    //
    // Whole measurements are kept rather than component minima: the load cost
    // is a quotient of a time and a key count, and mixing the numerator of
    // one attempt with the denominator of another would compare work that was
    // never done together. The key count varies between attempts by design,
    // because publication now depends on how long the scan has been running.
    if (attempts < 1) {
        return QStringLiteral("a load must be attempted at least once");
    }
    LoadMeasurement cheapest;
    for (int attempt = 0; attempt < attempts; ++attempt) {
        LoadMeasurement attempted;
        const QString failure = measureDirectoryLoad(entryCount, settleBudget, attempted);
        if (!failure.isEmpty()) {
            return failure;
        }
        const qint64 elapsed = attempted.firstScanMilliseconds + attempted.refreshMilliseconds;
        const qint64 best = cheapest.firstScanMilliseconds + cheapest.refreshMilliseconds;
        if (attempt == 0 || elapsed < best) {
            cheapest = attempted;
        }
    }
    measurement = cheapest;
    return {};
}
void DirectoryListModelCostTest::filteringCostsTheSameScatteredAsContiguous() {
    // Removing a scattered set costs what removing a contiguous block of the
    // same listing costs. Publishing each contiguous run separately did not:
    // the key index and the selection are both derived from every row, a
    // removal renumbers every row after it, and rebuilding them once per run
    // made a filter over a large directory grow with the square of its size.
    // Typing in the filter box is exactly a scattered removal.
    //
    // Three bounds, because no one of them holds the shape alone.
    //
    // The count of index rebuilds carries the mechanism. It is exact and
    // machine-independent, and it is the quantity that grew: a fixed number
    // per update against one per removal run. Measured 2 contiguous and 3
    // scattered here, against 2 and 2,898 for per-run publication.
    //
    // The ratio between the two filters carries the shape without depending
    // on machine speed, because both halves run on the same machine in the
    // same build. An instrumented build is roughly twenty times slower and
    // the ratio does not move. Measured 0.9 healthy at this size, against 29
    // for per-run publication.
    //
    // Wall clock catches only the catastrophic case, and is bounded per build
    // for that reason.
    constexpr quint64 indexRebuildCeiling = 8;
    constexpr double scatteredCostRatioCeiling = 4.0;
    const qint64 budgetMilliseconds = timingBudget(600, 12000);
    constexpr int entryCount = 16000;

    QTemporaryDir fixture;
    QVERIFY(fixture.isValid());
    const fs::path root = fixture.path().toStdString();
    // Two families of equal size. A needle matching one family prefix removes
    // a single block; a needle matching a digit removes a set spread through
    // both, which is many runs rather than one.
    for (int index = 0; index < entryCount / 2; ++index) {
        writeFile(root / ("alpha-" + std::to_string(index)));
        writeFile(root / ("bravo-" + std::to_string(index)));
    }

    DirectoryListModel model;
    model.setPath(fixture.path());
    QVERIFY(waitForIdleWithin(model, 30 * budgetMilliseconds));
    QCOMPARE(model.rowCount(), entryCount);
    // The watcher is stopped so a delivery cannot land inside a measurement.
    model.watchService_.stop();

    // Counted while every row is still presented, because a run count is a
    // property of the removal the filter is about to perform.
    const int contiguousRuns = ModelProbe::removalRunCount(model, QStringLiteral("alpha"));

    QElapsedTimer timer;
    const quint64 buildsBeforeContiguous = model.entryRowIndexBuilds_;
    timer.start();
    model.setFilterText(QStringLiteral("alpha"));
    const qint64 contiguousMilliseconds = timer.elapsed();
    const quint64 contiguousRebuilds = model.entryRowIndexBuilds_ - buildsBeforeContiguous;
    const int contiguousRows = model.rowCount();
    QCOMPARE(ModelProbe::rowKeyIndexMismatch(model), QString{});

    model.setFilterText(QString{});
    QCOMPARE(model.rowCount(), entryCount);

    const int scatteredRuns = ModelProbe::removalRunCount(model, QStringLiteral("7"));

    const quint64 buildsBeforeScattered = model.entryRowIndexBuilds_;
    timer.restart();
    model.setFilterText(QStringLiteral("7"));
    const qint64 scatteredMilliseconds = timer.elapsed();
    const quint64 scatteredRebuilds = model.entryRowIndexBuilds_ - buildsBeforeScattered;
    const int scatteredRows = model.rowCount();
    QCOMPARE(ModelProbe::rowKeyIndexMismatch(model), QString{});

    // The fixture is only meaningful if the two filters really do remove
    // comparable numbers of rows in incomparable numbers of runs. A scattered
    // filter that happened to remove a block would pass every bound below
    // while testing nothing.
    QCOMPARE(contiguousRows, entryCount / 2);
    QVERIFY(scatteredRows > entryCount / 8);
    QCOMPARE(contiguousRuns, 1);
    QVERIFY2(scatteredRuns > 100,
             qPrintable(QStringLiteral("scattered filter removed %1 runs").arg(scatteredRuns)));

    const double ratio = static_cast<double>(std::max<qint64>(scatteredMilliseconds, 1)) /
                         static_cast<double>(std::max<qint64>(contiguousMilliseconds, 1));

    // Reported unconditionally so a failure arrives with the measurements
    // that produced it instead of only the bounds it missed.
    qInfo("%d entries: contiguous filter %lld ms, %d rows removed in %d run, %llu index rebuilds",
          entryCount, static_cast<long long>(contiguousMilliseconds), entryCount - contiguousRows,
          contiguousRuns, static_cast<unsigned long long>(contiguousRebuilds));
    qInfo("%d entries: scattered filter %lld ms, %d rows removed in %d runs, %llu index rebuilds",
          entryCount, static_cast<long long>(scatteredMilliseconds), entryCount - scatteredRows,
          scatteredRuns, static_cast<unsigned long long>(scatteredRebuilds));
    qInfo(
        "scattered/contiguous cost ratio %.2f, ceiling %.2f; rebuild ceiling %llu; budget %lld ms "
        "for %s build",
        ratio, scatteredCostRatioCeiling, static_cast<unsigned long long>(indexRebuildCeiling),
        static_cast<long long>(budgetMilliseconds),
        runningUnderAddressSanitizer() ? "an instrumented" : "a release");

    // Bounded below as well as above. An upper bound alone is satisfied by a
    // counter that has stopped counting, which would retire the instrument
    // while leaving the gate green.
    QVERIFY(contiguousRebuilds > 0);
    QVERIFY(scatteredRebuilds > 0);
    QVERIFY(contiguousRebuilds < indexRebuildCeiling);
    QVERIFY(scatteredRebuilds < indexRebuildCeiling);
    QVERIFY(ratio < scatteredCostRatioCeiling);
    QVERIFY(contiguousMilliseconds < budgetMilliseconds);
    QVERIFY(scatteredMilliseconds < budgetMilliseconds);
}
void DirectoryListModelCostTest::largeDirectoryLoadStaysWithinBudget() {
    // Directories large enough for reconciliation to dominate the fixed cost
    // of starting a scan. Every other case in this file uses a handful of
    // entries, which is why a load that grew with the square of the directory
    // size went unnoticed.
    //
    // Five bounds, because no one of them holds the shape on its own, and
    // because the first three bound a PROXY. Key construction is what the
    // model's cost is made of, but only while the model is the only thing
    // spending time. Scanned paths are already absolute and normal, so
    // `QString::fromStdString(entry.path.string())` produces a byte-identical
    // key without normalizing anything, and a linear rescan restored that way
    // was measured at 12.9 times the healthy load with the key count
    // unchanged to the digit and every count-based bound still green. The
    // last two bounds are there because of that, and neither depends on the
    // model calling entryKey at all.
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
    //
    // The fourth bound denominates the load's cost in the operation it is
    // made of, by timing the same process building the number of keys the
    // load reports having built. The question it asks is whether the reported
    // count explains the time spent, so work that is uncounted because it was
    // spelled differently inflates the numerator while leaving the
    // denominator alone. It is machine-independent for the same reason a
    // ratio is, and it survives instrumentation because both halves pay it,
    // though not equally: measured 4.04 to 4.17 release and 5.47 to 5.52
    // under the sanitizer, against 50.2 and 98.6 for the restored rescan. The
    // ceiling sits above the worst healthy reading by rather more than the
    // gap between the two builds, and still a factor of four below the
    // cheapest defective one.
    //
    // The fifth bounds how the elapsed load grows when the directory doubles,
    // which catches a change of exponent without reference to any counter:
    // 1.94 to 2.11 healthy and 3.82 to 3.87 for the same rescan. It is kept
    // alongside the fourth because they fail differently — this one sees a
    // shape a small constant would hide, and that one sees a constant this
    // would not.
    //
    // Both timed bounds read the cheapest of several loads at each size, for
    // the reason given where the attempts are run: the healthy and defective
    // readings are close enough together that a single reading's upward
    // scatter reaches the ceiling on its own.
    constexpr int loadAttempts = 3;
    constexpr quint64 keyBuildsPerEntryCeiling = 60;
    constexpr double keyBuildGrowthCeiling = 2.8;
    constexpr double loadCostCeiling = 12.0;
    constexpr double loadCostFloor = 0.5;
    constexpr double elapsedGrowthCeiling = 2.8;
    constexpr double elapsedGrowthFloor = 1.2;
    const qint64 budgetMilliseconds = timingBudget(3000, 20000);
    constexpr int smallerEntryCount = 4000;
    constexpr int largerEntryCount = 2 * smallerEntryCount;

    LoadMeasurement smaller;
    LoadMeasurement larger;
    const SettleBudget settleBudget{.milliseconds = 4 * budgetMilliseconds};
    QCOMPARE(measureCheapestDirectoryLoad(smallerEntryCount, settleBudget, loadAttempts, smaller),
             QString{});
    QCOMPARE(measureCheapestDirectoryLoad(largerEntryCount, settleBudget, loadAttempts, larger),
             QString{});

    const double growth = static_cast<double>(larger.keyBuilds) /
                          static_cast<double>(std::max<quint64>(smaller.keyBuilds, 1));
    const quint64 keyBuildCeiling =
        keyBuildsPerEntryCeiling * static_cast<quint64>(largerEntryCount);

    // Reported unconditionally so a failure arrives with the measurements that
    // produced it instead of only the bounds it missed.
    qInfo("%d entries, cheapest of %d loads: scan %lld ms, refresh %lld ms, %llu keys built",
          smallerEntryCount, loadAttempts, static_cast<long long>(smaller.firstScanMilliseconds),
          static_cast<long long>(smaller.refreshMilliseconds),
          static_cast<unsigned long long>(smaller.keyBuilds));
    qInfo("%d entries, cheapest of %d loads: scan %lld ms, refresh %lld ms, %llu keys built, "
          "ceiling %llu",
          largerEntryCount, loadAttempts, static_cast<long long>(larger.firstScanMilliseconds),
          static_cast<long long>(larger.refreshMilliseconds),
          static_cast<unsigned long long>(larger.keyBuilds),
          static_cast<unsigned long long>(keyBuildCeiling));
    const qint64 largerElapsed = larger.firstScanMilliseconds + larger.refreshMilliseconds;
    const qint64 smallerElapsed = smaller.firstScanMilliseconds + smaller.refreshMilliseconds;
    const qint64 calibrationMilliseconds = timeKeyConstructions(larger.keyBuilds);
    const double loadCost = static_cast<double>(largerElapsed) /
                            static_cast<double>(std::max<qint64>(calibrationMilliseconds, 1));
    const double elapsedGrowth = static_cast<double>(largerElapsed) /
                                 static_cast<double>(std::max<qint64>(smallerElapsed, 1));

    qInfo("%llu key constructions take %lld ms in this process; the load took %lld ms, %.2f times "
          "as long, ceiling %.2f",
          static_cast<unsigned long long>(larger.keyBuilds),
          static_cast<long long>(calibrationMilliseconds), static_cast<long long>(largerElapsed),
          loadCost, loadCostCeiling);
    qInfo("elapsed load growth %.2f across a doubled directory, ceiling %.2f", elapsedGrowth,
          elapsedGrowthCeiling);
    qInfo("key construction growth %.2f across a doubled directory, ceiling %.2f, budget %lld ms "
          "for %s build",
          growth, keyBuildGrowthCeiling, static_cast<long long>(budgetMilliseconds),
          runningUnderAddressSanitizer() ? "an instrumented" : "a release");

    // Bounded below as well as above, for the same reason the index-rebuild
    // count is: a counter that stopped counting reads zero and passes any
    // ceiling. Every presented row is keyed at least once per update, so the
    // entry count is a floor the healthy figure clears by an order of
    // magnitude.
    QVERIFY(larger.keyBuilds > static_cast<quint64>(largerEntryCount));
    QVERIFY(smaller.keyBuilds > static_cast<quint64>(smallerEntryCount));
    QVERIFY(larger.keyBuilds < keyBuildCeiling);
    QVERIFY(growth < keyBuildGrowthCeiling);
    QVERIFY(smaller.firstScanMilliseconds < budgetMilliseconds);
    QVERIFY(smaller.refreshMilliseconds < budgetMilliseconds);
    QVERIFY(larger.firstScanMilliseconds < budgetMilliseconds);
    QVERIFY(larger.refreshMilliseconds < budgetMilliseconds);

    // The two cost bounds, both bounded on each side. A floor matters more
    // here than anywhere else in this file: the ratios are quotients of two
    // measurements, and a numerator that stopped being measured reads zero
    // and satisfies any ceiling.
    QVERIFY(calibrationMilliseconds > 0);
    QVERIFY(largerElapsed > 0);
    QVERIFY(smallerElapsed > 0);
    QVERIFY(loadCost > loadCostFloor);
    QVERIFY(loadCost < loadCostCeiling);
    QVERIFY(elapsedGrowth > elapsedGrowthFloor);
    QVERIFY(elapsedGrowth < elapsedGrowthCeiling);
}
void DirectoryListModelCostTest::watchDeliveryCostsOneLookupPerDeliveredName() {
    // A watch burst used to search the whole scanned listing once per
    // delivered name, rebuilding every candidate's key inside the comparison,
    // so a burst cost the product of its size and the directory's. The same
    // machine-independent instrument the load gate uses holds this: key
    // construction is now proportional to the burst plus the presented rows,
    // not to their product.
    constexpr int entryCount = 800;
    constexpr int deliveredCount = 400;

    QTemporaryDir fixture;
    QVERIFY(fixture.isValid());
    const fs::path root = fixture.path().toStdString();
    for (int index = 0; index < entryCount; ++index) {
        writeFile(root / ("entry-" + std::to_string(index)), "a");
    }

    DirectoryListModel model;
    model.setPath(fixture.path());
    QVERIFY(waitForIdleWithin(model, 10000));
    QCOMPARE(model.rowCount(), entryCount);
    model.watchService_.stop();

    std::vector<odysea::core::Entry> delivered;
    delivered.reserve(static_cast<std::size_t>(deliveredCount));
    for (int index = 0; index < deliveredCount; ++index) {
        const fs::path entry = root / ("entry-" + std::to_string(index));
        writeFile(entry, "grown well past its first size");
        delivered.push_back(odysea::core::make_entry(fs::directory_entry(entry)));
    }

    const quint64 before = model.entryKeyBuilds_;
    model.applyWatchUpdate(DirectoryWatchUpdate{.token = model.watchToken_,
                                                .directory = root,
                                                .removedNames = {},
                                                .updatedEntries = std::move(delivered),
                                                .renamedEntries = {},
                                                .error = {},
                                                .rescanRequired = false});
    const quint64 spent = model.entryKeyBuilds_ - before;

    // Four times the entries the update could legitimately have to key, which
    // leaves room for the reconciliation the update ends with and still sits
    // two orders of magnitude below a search per delivered name.
    const quint64 ceiling = 4 * static_cast<quint64>(entryCount + deliveredCount);
    qInfo("watch burst of %d names against %d entries built %llu keys, ceiling %llu",
          deliveredCount, entryCount, static_cast<unsigned long long>(spent),
          static_cast<unsigned long long>(ceiling));
    QVERIFY(spent < ceiling);

    QCOMPARE(model.rowCount(), entryCount);
    QCOMPARE(ModelProbe::scannedNameIndexMismatch(model), QString{});
    QCOMPARE(ModelProbe::rowKeyIndexMismatch(model), QString{});
    const int updatedRow = rowForName(model, QStringLiteral("entry-0"));
    QVERIFY(updatedRow >= 0);
    QCOMPARE(model.data(model.index(updatedRow), DirectoryListModel::SizeRole).toULongLong(),
             static_cast<qulonglong>(fs::file_size(root / "entry-0")));
}
void DirectoryListModelCostTest::heldEntriesAppearOnceTheyHaveWaitedAsLongAsTheScanHasRun() {
    // A scan publishes when the entries it holds amount to a quarter of the
    // listing, which bounds what is held in entries and, while entries keep
    // arriving at a steady rate, in time as well. It bounds nothing once that
    // rate falls: the held quarter then waits for a source that has slowed
    // down to produce another quarter. The clock rule publishes what has
    // waited longer than the scan has been running, which is the case above
    // and only that case.
    //
    // Time is supplied rather than spent. A rule about waiting is otherwise
    // testable only by waiting, which decides by machine load and would take
    // the intervals below in real seconds.
    constexpr int presentedCount = 400;
    constexpr int deliverySize = 32;

    QTemporaryDir fixture;
    QVERIFY(fixture.isValid());
    const fs::path root = fixture.path().toStdString();
    for (int index = 0; index < presentedCount; ++index) {
        writeFile(root / ("entry-" + std::to_string(index)));
    }

    DirectoryListModel model;
    model.setPath(fixture.path());
    QVERIFY(waitForIdleWithin(model, 10000));
    QCOMPARE(model.rowCount(), presentedCount);
    model.watchService_.stop();

    // Deliveries stay below the size interval throughout, so every
    // publication below is the clock's doing and not the interval's.
    QCOMPARE(model.scanPublishInterval(), std::size_t{128});
    QVERIFY(deliverySize < static_cast<int>(model.scanPublishInterval()));

    // The production clock, checked before it is replaced. The rule reads a
    // timer that starting a scan is responsible for starting, and a timer
    // that was never started reports nothing: the rule would then never fire,
    // and every step below would still pass on its supplied timeline.
    QVERIFY(model.scanClock_.isValid());
    const qint64 beforeWaiting = model.scanElapsedMilliseconds_();
    QTest::qWait(5);
    QVERIFY(model.scanElapsedMilliseconds_() > beforeWaiting);

    qint64 now = 0;
    model.scanElapsedMilliseconds_ = [&now] { return now; };

    int nextEntry = presentedCount;
    const auto deliverAt = [&model, &now, &nextEntry, &root](qint64 milliseconds) {
        now = milliseconds;
        std::vector<odysea::core::Entry> delivery;
        delivery.reserve(deliverySize);
        for (int index = 0; index < deliverySize; ++index) {
            const fs::path path = root / ("entry-" + std::to_string(nextEntry++));
            writeFile(path);
            delivery.push_back(odysea::core::make_entry(fs::directory_entry(path)));
        }
        model.receiveScanBatch(model.activeScanToken_, std::move(delivery));
    };

    // Held: the floor has not passed, so a scan is not made to republish its
    // listing on every delivery in its opening moments.
    deliverAt(100);
    QCOMPARE(model.rowCount(), presentedCount);
    QCOMPARE(model.pendingScanEntries_.size(), std::size_t{deliverySize});

    // Still held after 200 ms of waiting, which is the floor's doing: the
    // scan has run for 300 ms, so a rule with no floor would publish here.
    deliverAt(300);
    QCOMPARE(model.rowCount(), presentedCount);
    QCOMPARE(model.pendingScanEntries_.size(), 2 * static_cast<std::size_t>(deliverySize));

    // Published: 300 ms of waiting clears the floor.
    deliverAt(400);
    QCOMPARE(model.rowCount(), presentedCount + (3 * deliverySize));
    QVERIFY(model.pendingScanEntries_.empty());
    QCOMPARE(ModelProbe::rowKeyIndexMismatch(model), QString{});

    // The second hold begins at 500 ms, so it runs to 1,000 ms rather than to
    // the floor. This is what separates the shipped rule from one that
    // measures the wait from the start of the scan: that one publishes at
    // 800 ms, 800 ms of scan having passed a 250 ms floor long before.
    deliverAt(500);
    QCOMPARE(model.rowCount(), presentedCount + (3 * deliverySize));
    deliverAt(800);
    QCOMPARE(model.rowCount(), presentedCount + (3 * deliverySize));
    QCOMPARE(model.pendingScanEntries_.size(), 2 * static_cast<std::size_t>(deliverySize));

    deliverAt(1100);
    QCOMPARE(model.rowCount(), presentedCount + (6 * deliverySize));
    QVERIFY(model.pendingScanEntries_.empty());
    QCOMPARE(ModelProbe::rowKeyIndexMismatch(model), QString{});

    // Named rather than counted: a listing of the right size assembled from
    // the wrong entries passes a count on its own.
    for (int index = presentedCount; index < nextEntry; ++index) {
        QVERIFY2(rowForName(model, QStringLiteral("entry-%1").arg(index)) >= 0,
                 qPrintable(QStringLiteral("entry-%1 is missing").arg(index)));
    }
}
void DirectoryListModelCostTest::publishingOnTimeAloneStaysLogarithmicInScanDuration() {
    // What makes the clock rule affordable, held as a property rather than as
    // a wall-clock reading.
    //
    // A publication costs work proportional to the whole listing, so it takes
    // longer as the listing grows. A fixed deadline therefore bounds nothing:
    // once a publication takes longer than the deadline, the next delivery is
    // already overdue, every delivery publishes, and the load costs the
    // square of the entry count again. Measured on a 32,000-entry load, a
    // 5 ms deadline turned 22 publications into 223 and 244 ms into 5,524 ms.
    //
    // Requiring a hold to have lasted as long as the scan has run makes each
    // publication on time alone wait as long as every one before it, so their
    // number grows with the logarithm of the scan's duration rather than in
    // proportion to it. A ten-minute scan is the interesting length because
    // it is where the two answers are furthest apart.
    constexpr qint64 scanDurationMilliseconds = 600000;
    constexpr qint64 deliveryIntervalMilliseconds = 10;
    constexpr qint64 fixedDeadlineMilliseconds = DirectoryListModel::kScanHoldFloorMilliseconds;

    int rulePublications = 0;
    qint64 holdStart = -1;
    int fixedPublications = 0;
    qint64 fixedHoldStart = -1;
    for (qint64 now = 0; now <= scanDurationMilliseconds; now += deliveryIntervalMilliseconds) {
        if (holdStart < 0) {
            holdStart = now;
        }
        if (DirectoryListModel::scanHoldExpired(holdStart, now)) {
            ++rulePublications;
            holdStart = -1;
        }
        if (fixedHoldStart < 0) {
            fixedHoldStart = now;
        }
        if (now - fixedHoldStart >= fixedDeadlineMilliseconds) {
            ++fixedPublications;
            fixedHoldStart = -1;
        }
    }

    // One publication per doubling of the scan's duration past the floor,
    // plus the first. Written as the arithmetic rather than as the number it
    // produces, so a change to the floor or to the growth rule is reflected
    // here instead of papered over.
    int logarithmicCeiling = 1;
    for (qint64 reach = fixedDeadlineMilliseconds; reach <= scanDurationMilliseconds; reach *= 2) {
        ++logarithmicCeiling;
    }

    qInfo("a %lld ms scan delivering every %lld ms publishes on time alone %d times under the "
          "shipped rule, ceiling %d, against %d under a fixed %lld ms deadline",
          static_cast<long long>(scanDurationMilliseconds),
          static_cast<long long>(deliveryIntervalMilliseconds), rulePublications,
          logarithmicCeiling, fixedPublications, static_cast<long long>(fixedDeadlineMilliseconds));

    // Bounded below as well as above: a rule that never fires publishes
    // nothing on time and satisfies any ceiling while leaving the tail
    // exactly where it was.
    QVERIFY(rulePublications > 0);
    QVERIFY(rulePublications <= logarithmicCeiling);
    // The contrasting count is exact rather than bounded: a fixed deadline
    // holds for the deadline plus the delivery that notices it has passed, so
    // a scan of a given length publishes a fixed number of times whatever its
    // size, and that number is what makes the cost grow with the square of
    // the listing.
    QCOMPARE(fixedPublications,
             static_cast<int>(scanDurationMilliseconds /
                              (fixedDeadlineMilliseconds + deliveryIntervalMilliseconds)));
    QVERIFY(fixedPublications > 100 * rulePublications);
}
QTEST_GUILESS_MAIN(DirectoryListModelCostTest)

#include "tst_directory_list_model_cost.moc"
