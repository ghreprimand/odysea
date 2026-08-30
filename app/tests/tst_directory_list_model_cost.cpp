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
    ///
    /// Both clocks are kept for each half. The processor figure is what the
    /// budget is asserted against; the wall figure is reported beside it, so
    /// a failure arrives with both readings and the difference between them
    /// is visible rather than inferred.
    struct LoadMeasurement {
        qint64 firstScanMilliseconds = 0;
        qint64 refreshMilliseconds = 0;
        qint64 firstScanProcessorMicroseconds = 0;
        qint64 refreshProcessorMicroseconds = 0;
        quint64 keyBuilds = 0;

        /// Processor time over both halves, or -1 when either could not be
        /// read. A clock that stopped answering has to stay distinguishable
        /// from one reporting no work, because the two would otherwise
        /// satisfy the same ceiling.
        [[nodiscard]] qint64 processorMicroseconds() const noexcept {
            if (firstScanProcessorMicroseconds < 0 || refreshProcessorMicroseconds < 0) {
                return -1;
            }
            return firstScanProcessorMicroseconds + refreshProcessorMicroseconds;
        }
    };

    static QString measureDirectoryLoad(int entryCount, SettleBudget settleBudget,
                                        LoadMeasurement& measurement);

  private slots:
    void largeDirectoryLoadStaysWithinBudget();
    void filteringCostsTheSameScatteredAsContiguous();
    void watchDeliveryCostsOneLookupPerDeliveredName();
    void heldEntriesAppearOnceTheyHaveWaitedAsLongAsTheScanHasRun();
    void publishingOnTimeAloneStaysLogarithmicInScanDuration();
};
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
    ProcessCpuTimer processor;
    timer.start();
    processor.start();
    model.setPath(fixture.path());
    if (!waitForIdleWithin(model, settleBudget.milliseconds)) {
        return QStringLiteral("first scan of %1 entries did not settle").arg(entryCount);
    }
    measurement.firstScanMilliseconds = timer.elapsed();
    measurement.firstScanProcessorMicroseconds = processor.elapsedMicroseconds();
    if (model.rowCount() != entryCount) {
        return QStringLiteral("first scan presented %1 of %2 entries")
            .arg(model.rowCount())
            .arg(entryCount);
    }

    timer.restart();
    processor.restart();
    model.refresh();
    if (!waitForIdleWithin(model, settleBudget.milliseconds)) {
        return QStringLiteral("refresh of %1 entries did not settle").arg(entryCount);
    }
    measurement.refreshMilliseconds = timer.elapsed();
    measurement.refreshProcessorMicroseconds = processor.elapsedMicroseconds();
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
    // The shape is held by a count, and the size of the job by a budget of
    // processor time. Neither is a reading of how long the case took, and
    // that is the point: a ratio of wall-clock readings was asserted here
    // until it failed under a parallel battery at a growth of 5.63 while the
    // key count in the same run read 2.03, and the measurements that followed
    // said the ratio had never been a property of the model.
    //
    // WHAT WAS MEASURED, because a bound removed on an argument is a bound
    // removed on nothing. 1,092 loads were taken, 546 at each size, in both
    // builds and at four levels of company: alone, beside three other copies
    // of this case, and beside thirty-one of them on a thirty-two-way
    // machine, then alone and beside five in the instrumented build.
    //
    //   - The key count read 60,768 at 4,000 entries and 123,456 at 8,000 in
    //     every one of those loads, including the instrumented build, which
    //     is twenty-two times slower. Not a spread: the same two figures,
    //     546 times each.
    //   - Wall-clock growth read 1.90 to 2.26 alone and beside three, and
    //     1.40 to 4.05 beside thirty-one.
    //   - Processor-time growth, which removes the intervals the machine
    //     spent elsewhere, read 1.79 to 2.25 beside three and 1.67 to 3.60
    //     beside thirty-one. Better than the wall clock and still past the
    //     2.80 ceiling, because a shared machine really does become
    //     superlinear in directory size: the larger load has the larger
    //     working set and pays more of the contention, in cache, in
    //     allocation, and in the kernel.
    //
    // So a growth ratio of any clock mixes two exponents — the model's and
    // the machine's — and cannot say which one moved. Taking the cheapest of
    // several attempts, which is what this case used to do, does not survive
    // that: it lowers both readings together and leaves the ratio between
    // them where it was. The clock ratios are gone rather than widened, and
    // the attempts with them, because a third of the readings were being
    // discarded to stabilize a quantity that was never stable.
    //
    // WHAT THE COUNT DOES NOT COVER, stated because the removed bounds
    // covered part of it. Scanned paths are already absolute and normal, so
    // spelling a key by hand produces a byte-identical key that goes
    // uncounted, and a linear rescan restored that way was once measured at
    // 12.9 times the healthy load with the key count unchanged to the digit.
    // That hole is closed where it belongs, by key_construction_guard, which
    // holds both spellings of a key inside the functions allowed to build
    // one. A clock was standing in for a rule about the source.
    //
    // The count of key constructions carries the algorithmic shape. Each
    // construction normalizes a path and allocates a string, which is what
    // the cost is made of. Measured over a load and a refresh it is 15.2 per
    // entry at 4,000 and 15.4 at 8,000, and that flat rate is the property
    // worth holding, so it is bounded directly.
    //
    // The ratio between the two sizes bounds the exponent rather than the
    // rate. A growth ratio was useless while both the healthy and the
    // defective states were quadratic, because every reading landed near four.
    // It discriminates now precisely because publishing on a growing interval
    // moved the healthy state to linear: doubling the entry count doubles the
    // count here and quadruples it if the interval stops growing. Measured
    // 2.03 healthy against 3.74 for a fixed publishing interval, 3.66 for a
    // linear rescan per delivered entry.
    //
    // The ceiling keeps its distance from the exact reading rather than being
    // drawn tight against it. Publication is decided by the size interval at
    // these directory sizes, and the clock rule never fires, which is why the
    // count is exact; a stall long enough to fire it would add one full
    // keying of the listing, and the headroom is there to absorb that without
    // reaching the 3.74 a lost interval produces.
    //
    // The budget is processor time summed over the threads the load uses,
    // not elapsed time. It bounds how large the job is, and a run that was
    // descheduled while the rest of a battery ran did not do more work. Read
    // 109 ms alone and 271 ms beside thirty-one copies, against a 3,000 ms
    // release ceiling; 2.35 s instrumented against 20,000 ms. Elapsed time is
    // reported beside it and is used for one thing only, the settle timeout,
    // which sits four times above the budget again.
    constexpr quint64 keyBuildsPerEntryCeiling = 60;
    constexpr double keyBuildGrowthCeiling = 2.8;
    constexpr double keyBuildGrowthFloor = 1.5;
    const qint64 budgetMilliseconds = timingBudget(3000, 20000);
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

    const qint64 budgetMicroseconds = budgetMilliseconds * 1000;

    // Reported unconditionally so a failure arrives with the measurements that
    // produced it instead of only the bounds it missed. Both clocks appear,
    // so a reader can see for themselves how far apart they ran.
    qInfo("%d entries: scan %lld ms, refresh %lld ms, %lld us of processor time, %llu keys built",
          smallerEntryCount, static_cast<long long>(smaller.firstScanMilliseconds),
          static_cast<long long>(smaller.refreshMilliseconds),
          static_cast<long long>(smaller.processorMicroseconds()),
          static_cast<unsigned long long>(smaller.keyBuilds));
    qInfo("%d entries: scan %lld ms, refresh %lld ms, %lld us of processor time, %llu keys built, "
          "ceiling %llu",
          largerEntryCount, static_cast<long long>(larger.firstScanMilliseconds),
          static_cast<long long>(larger.refreshMilliseconds),
          static_cast<long long>(larger.processorMicroseconds()),
          static_cast<unsigned long long>(larger.keyBuilds),
          static_cast<unsigned long long>(keyBuildCeiling));
    qInfo("key construction growth %.2f across a doubled directory, floor %.2f, ceiling %.2f; "
          "processor budget %lld us for %s build",
          growth, keyBuildGrowthFloor, keyBuildGrowthCeiling,
          static_cast<long long>(budgetMicroseconds),
          runningUnderAddressSanitizer() ? "an instrumented" : "a release");

    // Bounded below as well as above, for the same reason the index-rebuild
    // count is: a counter that stopped counting reads zero and passes any
    // ceiling. Every presented row is keyed at least once per update, so the
    // entry count is a floor the healthy figure clears by an order of
    // magnitude.
    QVERIFY(larger.keyBuilds > static_cast<quint64>(largerEntryCount));
    QVERIFY(smaller.keyBuilds > static_cast<quint64>(smallerEntryCount));
    QVERIFY(larger.keyBuilds < keyBuildCeiling);

    // The growth is bounded on both sides. It is a quotient of two counts,
    // and a denominator that grew or a numerator that stopped being counted
    // both satisfy a ceiling on their own; a load of twice the entries cannot
    // honestly cost less than half again as many keys as the smaller one.
    QVERIFY(growth > keyBuildGrowthFloor);
    QVERIFY(growth < keyBuildGrowthCeiling);

    // The processor budget, bounded below as well, because a clock that
    // stopped answering reports a negative interval and would otherwise pass
    // every ceiling here.
    QVERIFY(smaller.processorMicroseconds() > 0);
    QVERIFY(larger.processorMicroseconds() > 0);
    QVERIFY(smaller.processorMicroseconds() < budgetMicroseconds);
    QVERIFY(larger.processorMicroseconds() < budgetMicroseconds);
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

    // And a floor, because a ceiling on a counter is also satisfied by a
    // counter that stopped counting. Reading zero here would mean the burst
    // was applied without a single key being constructed, which no correct
    // path can do: the update ends by reconciling the listing against the
    // presented rows, and that reconciliation identifies a row by its key. So
    // the cost cannot fall below one key per presented row, and every entry in
    // this fixture is presented - the fixture holds no hidden or filtered
    // names. This instrument has read as a pass while dead before, on a
    // ceiling that had no floor under it.
    const quint64 floor = entryCount;

    qInfo("watch burst of %d names against %d entries built %llu keys, floor %llu, ceiling %llu",
          deliveredCount, entryCount, static_cast<unsigned long long>(spent),
          static_cast<unsigned long long>(floor), static_cast<unsigned long long>(ceiling));
    QVERIFY2(spent >= floor,
             qPrintable(QStringLiteral("a burst over %1 presented rows built only %2 keys; the "
                                       "reconciliation alone cannot cost fewer than %3")
                            .arg(entryCount)
                            .arg(spent)
                            .arg(floor)));
    QVERIFY2(spent < ceiling,
             qPrintable(QStringLiteral("a burst of %1 names over %2 entries built %3 keys, at or "
                                       "above the %4 a per-name search would cost")
                            .arg(deliveredCount)
                            .arg(entryCount)
                            .arg(spent)
                            .arg(ceiling)));

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
