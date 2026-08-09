// Headless tests for how the directory model acquires a listing and keeps it
// consistent: scanning, publication, folder-watch deliveries, entry identity,
// and the derived indexes over the scanned listing and the presented rows.
//
// Behaviour a person drives — selection, cursor movement, navigation input,
// activation, and filesystem operations — lives in
// tst_directory_list_model_interaction.cpp. What acquiring a listing is
// allowed to cost lives in tst_directory_list_model_cost.cpp. The split
// follows those boundaries rather than file size: these cases assert what the
// model does on its own, those assert what it does when it is asked to, and
// the cost cases assert how much it may spend doing either. The cost cases
// are also the slow ones, and separating them lets the invariants run at a
// speed that suits being run often.
#include "directory_list_model_test_support.hpp"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QPersistentModelIndex>
#include <QTemporaryDir>
#include <QThreadPool>

#include <algorithm>

namespace fs = std::filesystem;
using namespace odysea::apptest;

class DirectoryListModelTest : public QObject {
    Q_OBJECT

    // Empty when the selected row numbers agree with the selected keys over
    // the rows currently presented. Selection is derived state of the same
    // family as the key index, and it is repaired before an update returns,
    // so a lapse is observable only from inside a row signal.
    static QString selectionRowsMismatch(const DirectoryListModel& model);

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
    void overflowRequestsARescan();
    void destructionDropsQueuedWorkerCallbacks();
    void scatteredFilterMovesDepartingRowsBeforeRemovingThem();
    void aDepartedEntryTakesItsSelectionStateWithIt();
    void anUnknownIdentityFollowsNothing();
    void scannedNameIndexTracksEveryListingMutation();
    void heldBackScanEntriesReachTheCompletedListing();
    void republishedEntriesUpdateOneRowRatherThanAddingAnother();
    void supersededScanDropsTheEntriesItHeldBack();
    void rowKeyIndexTracksEveryRowMutation();
    void duplicateResolvedKeysCompareAgainstTheFirstRow();
};
QString DirectoryListModelTest::selectionRowsMismatch(const DirectoryListModel& model) {
    QSet<int> expected;
    for (int row = 0; row < model.rowCount(); ++row) {
        if (model.selectedEntryKeys_.contains(model.entryKeys_.at(static_cast<std::size_t>(row)))) {
            expected.insert(row);
        }
    }
    if (model.selectedRows_ != expected) {
        return QStringLiteral("selected rows %1 do not match the %2 selected keys present")
            .arg(model.selectedRows_.size())
            .arg(expected.size());
    }
    return {};
}

void DirectoryListModelTest::scatteredFilterMovesDepartingRowsBeforeRemovingThem() {
    // A scattered removal is published as a reorder that gathers the
    // departing rows at the end, then one removal of that block. Both halves
    // are observable, and both have to be right.
    //
    // A view holds persistent indexes on the rows it has realized. The
    // reorder must relocate them, so an index on a surviving row still names
    // that row afterwards, and an index on a departing row is invalidated by
    // the removal rather than left pointing at whatever moved into its place.
    // Getting that wrong is silent: the rows are correct and only the view's
    // idea of them is not.
    QTemporaryDir fixture;
    QVERIFY(fixture.isValid());
    const fs::path root = fixture.path().toStdString();
    // Named so that name order alternates keep and drop, which makes the
    // removal scattered rather than one block.
    for (int index = 0; index < 6; ++index) {
        writeFile(root / ("entry-" + std::to_string(index) +
                          (index % 2 == 0 ? "-keep.txt" : "-drop.txt")));
    }

    DirectoryListModel model;
    model.setPath(fixture.path());
    QTRY_VERIFY_WITH_TIMEOUT(!model.busy(), 5000);
    model.watchService_.stop();
    QCOMPARE(model.rowCount(), 6);

    const int survivorRow = rowForName(model, QStringLiteral("entry-4-keep.txt"));
    const int departingRow = rowForName(model, QStringLiteral("entry-3-drop.txt"));
    QVERIFY(survivorRow >= 0);
    QVERIFY(departingRow >= 0);
    const QPersistentModelIndex survivor = model.index(survivorRow);
    const QPersistentModelIndex departing = model.index(departingRow);

    // The derived state is checked from inside the reorder and the removal as
    // well as after them, because a view answers those signals by asking the
    // model for data.
    QString liveMismatch;
    int liveChecks = 0;
    const auto observe = [&model, &liveMismatch, &liveChecks] {
        ++liveChecks;
        if (liveMismatch.isEmpty()) {
            liveMismatch = ModelProbe::rowKeyIndexMismatch(model);
        }
        if (liveMismatch.isEmpty()) {
            liveMismatch = selectionRowsMismatch(model);
        }
    };
    connect(&model, &QAbstractItemModel::layoutChanged, &model, observe);
    connect(&model, &QAbstractItemModel::rowsRemoved, &model, observe);

    // Every row that leaves has to leave through a removal signal. A removal
    // that publishes fewer rows than it takes away lets the row count fall
    // silently, which a view has no way to notice.
    int rowsPublishedAsRemoved = 0;
    connect(&model, &QAbstractItemModel::rowsAboutToBeRemoved, &model,
            [&rowsPublishedAsRemoved](const QModelIndex&, int first, int last) {
                rowsPublishedAsRemoved += last - first + 1;
            });

    model.selectRow(survivorRow, Qt::NoModifier);
    model.selectRow(departingRow, Qt::ControlModifier);
    QCOMPARE(model.selectedCount(), 2);

    QSignalSpy resetSpy(&model, &QAbstractItemModel::modelReset);
    QSignalSpy removedSpy(&model, &QAbstractItemModel::rowsRemoved);
    QSignalSpy layoutAboutSpy(&model, &QAbstractItemModel::layoutAboutToBeChanged);
    QSignalSpy layoutSpy(&model, &QAbstractItemModel::layoutChanged);

    const int rowsBefore = model.rowCount();
    model.setFilterText(QStringLiteral("keep"));

    QCOMPARE(model.rowCount(), 3);
    QCOMPARE(resetSpy.count(), 0);
    // One removal, not one per run. Publishing three runs separately is the
    // cost this change exists to remove, so the count is pinned rather than
    // left to the timing bounds.
    QCOMPARE(removedSpy.count(), 1);
    QCOMPARE(rowsPublishedAsRemoved, rowsBefore - model.rowCount());
    // A reorder is announced before it happens. Publishing only the second
    // half leaves a view that never saved the state it is being told to
    // restore, and persistent indexes are the visible part of that state.
    QVERIFY(layoutSpy.count() >= 1);
    QCOMPARE(layoutAboutSpy.count(), layoutSpy.count());
    QCOMPARE(ModelProbe::rowKeyIndexMismatch(model), QString{});
    QCOMPARE(selectionRowsMismatch(model), QString{});
    QCOMPARE(model.selectedCount(), 1);

    QVERIFY(survivor.isValid());
    QCOMPARE(model.data(survivor, DirectoryListModel::NameRole).toString(),
             QStringLiteral("entry-4-keep.txt"));
    QCOMPARE(survivor.row(), rowForName(model, QStringLiteral("entry-4-keep.txt")));
    QVERIFY(!departing.isValid());

    QVERIFY(liveChecks > 0);
    QCOMPARE(liveMismatch, QString{});
}

void DirectoryListModelTest::aDepartedEntryTakesItsSelectionStateWithIt() {
    // A departed entry is dropped from the selection, the cursor, the range
    // anchor, and the rubber-band base. Leaving it behind in any of them is
    // invisible while the entry is gone, because those are keyed state and
    // the row no longer exists. It becomes visible when an entry of the same
    // name appears: it arrives already selected, or already under the cursor,
    // having inherited state from a file that no longer exists.
    QTemporaryDir fixture;
    QVERIFY(fixture.isValid());
    const fs::path root = fixture.path().toStdString();
    writeFile(root / "departing.txt", "d");
    writeFile(root / "staying.txt", "s");

    DirectoryListModel model;
    model.setPath(fixture.path());
    QTRY_VERIFY_WITH_TIMEOUT(!model.busy(), 5000);
    model.watchService_.stop();

    const int departingRow = rowForName(model, QStringLiteral("departing.txt"));
    QVERIFY(departingRow >= 0);
    model.selectRow(departingRow, Qt::NoModifier);
    model.beginRubberBand(true);
    const QString departingKey = model.keyForRow(departingRow);
    QVERIFY(!departingKey.isEmpty());
    QVERIFY(model.selectedEntryKeys_.contains(departingKey));
    QCOMPARE(model.currentEntryKey_, departingKey);
    QCOMPARE(model.selectionAnchorKey_, departingKey);
    QVERIFY(model.rubberBandBaseKeys_.contains(departingKey));

    fs::remove(root / "departing.txt");
    model.applyWatchUpdate(DirectoryWatchUpdate{.token = model.watchToken_,
                                                .directory = root,
                                                .removedNames = {"departing.txt"},
                                                .updatedEntries = {},
                                                .renamedEntries = {},
                                                .error = {},
                                                .rescanRequired = false});

    QCOMPARE(rowForName(model, QStringLiteral("departing.txt")), -1);
    QVERIFY(!model.selectedEntryKeys_.contains(departingKey));
    QVERIFY(model.currentEntryKey_ != departingKey);
    QVERIFY(model.selectionAnchorKey_ != departingKey);
    QVERIFY(!model.rubberBandBaseKeys_.contains(departingKey));
    QCOMPARE(model.selectedCount(), 0);

    // The observable half: a new entry of the same name is a different file
    // and arrives with none of the departed entry's state.
    writeFile(root / "departing.txt", "a different file entirely");
    model.applyWatchUpdate(DirectoryWatchUpdate{
        .token = model.watchToken_,
        .directory = root,
        .removedNames = {},
        .updatedEntries = {odysea::core::make_entry(fs::directory_entry(root / "departing.txt"))},
        .renamedEntries = {},
        .error = {},
        .rescanRequired = false});

    const int arrivedRow = rowForName(model, QStringLiteral("departing.txt"));
    QVERIFY(arrivedRow >= 0);
    QVERIFY(!model.rowSelected(arrivedRow));
    QCOMPARE(model.selectedCount(), 0);
    QVERIFY(model.currentIndex() != arrivedRow);
}
void DirectoryListModelTest::anUnknownIdentityFollowsNothing() {
    // An entry whose metadata could not be read has no identity, and the core
    // rule is that an unknown identity matches nothing, including another
    // unknown one. A departed entry with no identity must therefore not be
    // followed to a delivered entry with no identity: they are two files that
    // failed to be measured, not one file that moved.
    //
    // Both identities are cleared directly. Reaching this state from the
    // filesystem needs a metadata read to fail between the entry being seen
    // and being measured, which is a race rather than a condition a case can
    // arrange.
    QTemporaryDir fixture;
    QVERIFY(fixture.isValid());
    const fs::path root = fixture.path().toStdString();
    writeFile(root / "departing.txt", "d");
    writeFile(root / "arriving.txt", "a");

    DirectoryListModel model;
    model.setPath(fixture.path());
    QTRY_VERIFY_WITH_TIMEOUT(!model.busy(), 5000);
    model.watchService_.stop();

    const int departingRow = rowForName(model, QStringLiteral("departing.txt"));
    QVERIFY(departingRow >= 0);
    model.selectRow(departingRow, Qt::NoModifier);
    QCOMPARE(selectedName(model), QStringLiteral("departing.txt"));

    const auto departing =
        std::ranges::find(model.scannedEntries_, "departing.txt", &odysea::core::Entry::name);
    QVERIFY(departing != model.scannedEntries_.end());
    departing->identity = odysea::core::EntryIdentity{};
    QVERIFY(!departing->identity.known());

    odysea::core::Entry arriving =
        odysea::core::make_entry(fs::directory_entry(root / "arriving.txt"));
    arriving.identity = odysea::core::EntryIdentity{};
    QVERIFY(!arriving.identity.known());

    fs::remove(root / "departing.txt");
    model.applyWatchUpdate(DirectoryWatchUpdate{.token = model.watchToken_,
                                                .directory = root,
                                                .removedNames = {"departing.txt"},
                                                .updatedEntries = {arriving},
                                                .renamedEntries = {},
                                                .error = {},
                                                .rescanRequired = false});

    QCOMPARE(rowForName(model, QStringLiteral("departing.txt")), -1);
    const int arrivingRow = rowForName(model, QStringLiteral("arriving.txt"));
    QVERIFY(arrivingRow >= 0);
    QVERIFY(!model.rowSelected(arrivingRow));
    QCOMPARE(selectedName(model), QString());
    QCOMPARE(model.selectedCount(), 0);
}
void DirectoryListModelTest::scannedNameIndexTracksEveryListingMutation() {
    QTemporaryDir fixture;
    QVERIFY(fixture.isValid());
    const fs::path root = fixture.path().toStdString();
    const fs::path listing = root / "listing";
    const fs::path elsewhere = root / "elsewhere";
    fs::create_directories(listing);
    fs::create_directories(elsewhere);
    writeFile(listing / "alpha.txt", "a");
    writeFile(listing / "bravo.txt", "bb");
    writeFile(listing / "charlie.txt", "ccc");
    writeFile(listing / ".hidden.txt", "h");
    writeFile(elsewhere / "unrelated.txt");

    DirectoryListModel model;

    // Both indexes are checked from inside the signals as well as after them.
    // A view answers a row signal by asking the model for data, and the
    // scanned listing is what the presented rows were derived from, so a
    // listing left inconsistent mid-update is observable exactly there.
    QString liveMismatch;
    int liveChecks = 0;
    const auto observe = [&model, &liveMismatch, &liveChecks] {
        ++liveChecks;
        if (!liveMismatch.isEmpty()) {
            return;
        }
        liveMismatch = ModelProbe::scannedNameIndexMismatch(model);
        if (liveMismatch.isEmpty()) {
            liveMismatch = ModelProbe::rowKeyIndexMismatch(model);
        }
    };
    connect(&model, &QAbstractItemModel::rowsInserted, &model, observe);
    connect(&model, &QAbstractItemModel::rowsRemoved, &model, observe);

    model.setPath(QString::fromStdString(listing.string()));
    QVERIFY(waitForIdleWithin(model, 5000));
    QCOMPARE(ModelProbe::scannedNameIndexMismatch(model), QString{});
    // The hidden entry is in the scanned listing even though no row presents
    // it, which is why the presented index cannot answer for the listing.
    QCOMPARE(model.scannedEntries_.size(), std::size_t{4});
    QCOMPARE(model.rowCount(), 3);
    QVERIFY(model.scannedRowsByName_.contains(QStringLiteral(".hidden.txt")));

    model.watchService_.stop();

    // A rescan that replaces every row.
    fs::rename(listing / "alpha.txt", listing / "delta.txt");
    model.refresh();
    QVERIFY(waitForIdleWithin(model, 5000));
    QCOMPARE(ModelProbe::scannedNameIndexMismatch(model), QString{});
    QVERIFY(model.scannedRowsByName_.contains(QStringLiteral("delta.txt")));
    QVERIFY(!model.scannedRowsByName_.contains(QStringLiteral("alpha.txt")));
    model.watchService_.stop();

    // A burst that renames, removes, and updates in one delivery, including a
    // removal of the entry the presentation withholds.
    fs::rename(listing / "bravo.txt", listing / "echo.txt");
    fs::remove(listing / ".hidden.txt");
    writeFile(listing / "charlie.txt", "grown well past its first size");
    model.applyWatchUpdate(DirectoryWatchUpdate{
        .token = model.watchToken_,
        .directory = listing,
        .removedNames = {"bravo.txt", ".hidden.txt"},
        .updatedEntries = {odysea::core::make_entry(fs::directory_entry(listing / "echo.txt")),
                           odysea::core::make_entry(fs::directory_entry(listing / "charlie.txt"))},
        .renamedEntries = {DirectoryEntryRename{.oldName = "bravo.txt", .newName = "echo.txt"}},
        .error = {},
        .rescanRequired = false});
    QCOMPARE(ModelProbe::scannedNameIndexMismatch(model), QString{});
    QVERIFY(model.scannedRowsByName_.contains(QStringLiteral("echo.txt")));
    QVERIFY(!model.scannedRowsByName_.contains(QStringLiteral("bravo.txt")));
    QVERIFY(!model.scannedRowsByName_.contains(QStringLiteral(".hidden.txt")));
    QCOMPARE(model.scannedEntries_.size(), std::size_t{3});

    // A different directory, so a stale listing cannot survive the move.
    model.setPath(QString::fromStdString(elsewhere.string()));
    QVERIFY(waitForIdleWithin(model, 5000));
    QCOMPARE(ModelProbe::scannedNameIndexMismatch(model), QString{});
    QCOMPARE(model.scannedEntries_.size(), std::size_t{1});
    QVERIFY(!model.scannedRowsByName_.contains(QStringLiteral("echo.txt")));

    // A refresh with no path, so the index is cleared rather than left
    // behind. Navigation cannot return to an empty path, so the path is
    // cleared directly: this branch is reached in the shipped application by
    // refreshing a model that has not been given one, and clearing here is
    // what lets the case reach it with a listing already in place.
    model.path_.clear();
    model.refresh();
    QCOMPARE(ModelProbe::scannedNameIndexMismatch(model), QString{});
    QVERIFY(model.scannedRowsByName_.isEmpty());
    QCOMPARE(model.rowCount(), 0);

    QVERIFY(liveChecks > 0);
    QCOMPARE(liveMismatch, QString{});
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
            liveMismatch = ModelProbe::rowKeyIndexMismatch(model);
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
            liveMismatch = ModelProbe::rowKeyIndexMismatch(model);
        }
    };
    connect(&model, &QAbstractItemModel::rowsInserted, &model, observe);
    connect(&model, &QAbstractItemModel::rowsRemoved, &model, observe);
    connect(&model, &QAbstractItemModel::layoutChanged, &model, observe);

    model.setPath(fixture.path());
    QTRY_VERIFY_WITH_TIMEOUT(!model.busy(), 5000);
    QCOMPARE(ModelProbe::rowKeyIndexMismatch(model), QString{});

    // Reorder.
    model.setSortMode(DirectoryListModel::SortBySize);
    QCOMPARE(ModelProbe::rowKeyIndexMismatch(model), QString{});

    // Removal, then insertion, through the filter.
    model.setFilterText(QStringLiteral("alpha"));
    QCOMPARE(model.rowCount(), 1);
    QCOMPARE(ModelProbe::rowKeyIndexMismatch(model), QString{});
    model.setFilterText({});
    QCOMPARE(model.rowCount(), 3);
    QCOMPARE(ModelProbe::rowKeyIndexMismatch(model), QString{});

    // Insertion of rows the presentation previously withheld.
    model.setShowHidden(true);
    QCOMPARE(model.rowCount(), 4);
    QCOMPARE(ModelProbe::rowKeyIndexMismatch(model), QString{});

    // A rescan that replaces every row.
    fs::rename(root / "alpha.txt", root / "delta.txt");
    fs::remove(root / "charlie.txt");
    model.refresh();
    QTRY_VERIFY_WITH_TIMEOUT(!model.busy(), 5000);
    QCOMPARE(ModelProbe::rowKeyIndexMismatch(model), QString{});
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
    QCOMPARE(ModelProbe::rowKeyIndexMismatch(model), QString{});
    QVERIFY(rowForName(model, QStringLiteral("echo.txt")) >= 0);

    // An empty listing, so the index is cleared rather than left behind.
    model.setFilterText(QStringLiteral("no-such-entry"));
    QCOMPARE(model.rowCount(), 0);
    QCOMPARE(ModelProbe::rowKeyIndexMismatch(model), QString{});

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
                    liveMismatch = ModelProbe::rowKeyIndexMismatch(model);
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
    QCOMPARE(ModelProbe::rowKeyIndexMismatch(model), QString{});
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
