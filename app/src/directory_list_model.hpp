// Qt bridge: exposes odysea::core directory listings and shell state to QML.
//
// Filesystem behavior stays in the framework-free core. This adapter schedules
// core scans away from the GUI thread and translates their results into Qt
// model roles and navigation state.
#pragma once

#include <QAbstractListModel>
#include <QElapsedTimer>
#include <QFutureWatcher>
#include <QHash>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <vector>

#include "directory_watch_service.hpp"
#include "filesystem_operation_job.hpp"
#include "odysea/core/directory_model.hpp"
#include "odysea/core/directory_scanner.hpp"
#include "odysea/core/thumbnail_service.hpp"

class ThumbnailImageProvider;
class EntryLauncher;

namespace odysea::apptest {
// Declared here so the model can befriend the shared test probes once,
// instead of befriending each suite that needs to read its derived state.
struct ModelProbe;
} // namespace odysea::apptest

class DirectoryListModel : public QAbstractListModel {
    Q_OBJECT
    Q_PROPERTY(QString path READ path WRITE setPath NOTIFY pathChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
    Q_PROPERTY(QString errorString READ errorString NOTIFY errorStringChanged)
    Q_PROPERTY(bool showHidden READ showHidden WRITE setShowHidden NOTIFY showHiddenChanged)
    Q_PROPERTY(QString filterText READ filterText WRITE setFilterText NOTIFY filterTextChanged)
    Q_PROPERTY(int sortMode READ sortMode WRITE setSortMode NOTIFY sortModeChanged)
    Q_PROPERTY(int currentIndex READ currentIndex NOTIFY currentIndexChanged)
    Q_PROPERTY(int selectedCount READ selectedCount NOTIFY selectedCountChanged)
    Q_PROPERTY(QStringList selectedFileUrls READ selectedFileUrls NOTIFY selectedFileUrlsChanged)
    Q_PROPERTY(bool canGoBack READ canGoBack NOTIFY navigationChanged)
    Q_PROPERTY(bool canGoForward READ canGoForward NOTIFY navigationChanged)
    Q_PROPERTY(bool canGoUp READ canGoUp NOTIFY navigationChanged)
    Q_PROPERTY(int tabCount READ tabCount NOTIFY tabsChanged)
    Q_PROPERTY(int activeTab READ activeTab NOTIFY tabsChanged)
    Q_PROPERTY(int paneCount READ paneCount NOTIFY panesChanged)
    Q_PROPERTY(int activePane READ activePane NOTIFY panesChanged)
    Q_PROPERTY(QString statusMessage READ statusMessage NOTIFY statusMessageChanged)
    Q_PROPERTY(bool operationBusy READ operationBusy NOTIFY operationBusyChanged)
    Q_PROPERTY(
        QString operationErrorString READ operationErrorString NOTIFY operationErrorStringChanged)

  public:
    enum Roles {
        NameRole = Qt::UserRole + 1,
        IsDirRole,
        IsSymlinkRole,
        SizeRole,
        PathRole,
        SelectedRole,
        RecoveryEntryRole,
        ThumbnailSourceRole,
        ThumbnailLoadingRole
    };

    enum SortMode { SortByName = 0, SortBySize, SortByType };
    Q_ENUM(SortMode)

    enum ConflictMode { ConflictFail = 0, ConflictOverwrite, ConflictAutoRename };
    Q_ENUM(ConflictMode)

    explicit DirectoryListModel(QObject* parent = nullptr);
    explicit DirectoryListModel(EntryLauncher& entryLauncher, QObject* parent = nullptr);
    explicit DirectoryListModel(ThumbnailImageProvider& thumbnailProvider,
                                QObject* parent = nullptr);
    DirectoryListModel(ThumbnailImageProvider& thumbnailProvider,
                       odysea::core::ThumbnailProducer& thumbnailProducer,
                       odysea::core::ThumbnailStore& thumbnailStore,
                       odysea::core::ThumbnailServiceOptions options, QObject* parent = nullptr);
    ~DirectoryListModel() override;

    [[nodiscard]] int rowCount(const QModelIndex& parent = {}) const override;
    [[nodiscard]] QVariant data(const QModelIndex& index, int role) const override;
    [[nodiscard]] QHash<int, QByteArray> roleNames() const override;

    [[nodiscard]] QString path() const;
    void setPath(const QString& path);

    [[nodiscard]] bool busy() const noexcept;
    [[nodiscard]] QString errorString() const;
    [[nodiscard]] bool showHidden() const noexcept;
    void setShowHidden(bool showHidden);
    [[nodiscard]] QString filterText() const;
    void setFilterText(const QString& filterText);
    [[nodiscard]] int sortMode() const noexcept;
    void setSortMode(int sortMode);

    [[nodiscard]] int currentIndex() const noexcept;
    [[nodiscard]] int selectedCount() const noexcept;
    [[nodiscard]] bool canGoBack() const;
    [[nodiscard]] bool canGoForward() const;
    [[nodiscard]] bool canGoUp() const;

    [[nodiscard]] int tabCount() const noexcept;
    [[nodiscard]] int activeTab() const noexcept;
    [[nodiscard]] int paneCount() const noexcept;
    [[nodiscard]] int activePane() const noexcept;
    [[nodiscard]] QString statusMessage() const;
    [[nodiscard]] bool operationBusy() const noexcept;
    [[nodiscard]] QString operationErrorString() const;

    Q_INVOKABLE void refresh();
    Q_INVOKABLE void goBack();
    Q_INVOKABLE void goForward();
    Q_INVOKABLE void goUp();
    Q_INVOKABLE void activate(int row);
    Q_INVOKABLE void activateCurrent();
    Q_INVOKABLE void navigateToPath(const QString& path);
    Q_INVOKABLE bool navigateFromInput(const QString& input);
    Q_INVOKABLE static QVariantMap navigationCompletion(const QString& input);
    [[nodiscard]] static QString resolveNavigationInput(const QString& input);
    Q_INVOKABLE QVariantList breadcrumbSegments() const;
    [[nodiscard]] QStringList selectedFileUrls() const;
    Q_INVOKABLE bool rowSelected(int row) const;
    Q_INVOKABLE bool rowIsDirectory(int row) const;
    Q_INVOKABLE bool canDropSelection(const QString& destinationDirectory, bool move) const;
    Q_INVOKABLE bool dropSelection(const QString& destinationDirectory, bool move,
                                   int conflictMode);
    Q_INVOKABLE void requestThumbnail(int row);
    Q_INVOKABLE void releaseThumbnail(const QString& entryPath);

    Q_INVOKABLE void selectRow(int row, Qt::KeyboardModifiers modifiers);
    Q_INVOKABLE void moveCursor(int delta, bool extendSelection, bool preserveSelection);
    Q_INVOKABLE void moveCursorTo(int row, bool extendSelection, bool preserveSelection);
    Q_INVOKABLE bool selectByPrefix(const QString& prefix, bool cycle);
    Q_INVOKABLE void toggleCurrent();
    Q_INVOKABLE void selectAll();
    Q_INVOKABLE void clearSelection();
    Q_INVOKABLE void beginRubberBand(bool additive);
    Q_INVOKABLE void updateRubberBandSelection(const QVariantList& rows, int currentRow);
    Q_INVOKABLE void endRubberBand();

    Q_INVOKABLE QString tabLabel(int tabIndex) const;
    Q_INVOKABLE void addTab();
    Q_INVOKABLE void closeTab(int tabIndex);
    Q_INVOKABLE void activateTab(int tabIndex);
    Q_INVOKABLE void setDualPaneEnabled(bool enabled);
    Q_INVOKABLE void activatePane(int paneIndex);

    Q_INVOKABLE void requestCopy();
    Q_INVOKABLE void requestMove();
    Q_INVOKABLE void requestRename();
    Q_INVOKABLE void requestTrash();
    Q_INVOKABLE void performCopy(const QString& destinationDirectory, int conflictMode);
    Q_INVOKABLE void performMove(const QString& destinationDirectory, int conflictMode);
    Q_INVOKABLE void performRename(const QString& newName, int conflictMode);
    Q_INVOKABLE void performTrash();

  signals:
    void pathChanged();
    void busyChanged();
    void errorStringChanged();
    void showHiddenChanged();
    void filterTextChanged();
    void sortModeChanged();
    void currentIndexChanged();
    void selectedCountChanged();
    void selectedFileUrlsChanged();
    void navigationChanged();
    void tabsChanged();
    void panesChanged();
    void statusMessageChanged();
    void operationBusyChanged();
    void operationErrorStringChanged();
    void openRequested(const QString& path);
    void filesystemOperationRequested(const QString& operation, const QStringList& paths);

  private:
    // The model's cases are split by responsibility: how a listing is
    // acquired and kept consistent, what that acquisition is allowed to cost,
    // and what a person does with the rows once it is there. All three reach
    // internal state that has no public spelling, as do the probes they
    // share.
    friend class DirectoryListModelTest;
    friend class DirectoryListModelCostTest;
    friend class DirectoryListModelInteractionTest;
    friend class MillerColumnsModelTest;
    friend struct odysea::apptest::ModelProbe;

    struct TabState {
        QString path;
        QStringList backHistory;
        QStringList forwardHistory;
    };

    struct PaneState {
        std::vector<TabState> tabs{TabState{}};
        int activeTab = 0;
    };

    [[nodiscard]] TabState& currentTab();
    [[nodiscard]] const TabState& currentTab() const;
    [[nodiscard]] PaneState& currentPane();
    [[nodiscard]] const PaneState& currentPane() const;
    [[nodiscard]] QString normalizedPath(const QString& path) const;
    [[nodiscard]] QStringList selectedPaths() const;
    [[nodiscard]] QString entryKey(const std::filesystem::path& path) const;
    [[nodiscard]] QString entryKey(const odysea::core::Entry& entry) const;
    [[nodiscard]] std::filesystem::path normalizedFilesystemPath(const QString& path) const;
    [[nodiscard]] QString entryIdentity(const odysea::core::Entry& entry) const;
    [[nodiscard]] QString keyForRow(int row) const;
    [[nodiscard]] int rowForEntryKey(const QString& key) const;
    [[nodiscard]] std::optional<odysea::core::ThumbnailKey>
    thumbnailKeyForEntry(const odysea::core::Entry& entry) const;
    [[nodiscard]] odysea::core::OperationOptions operationOptions(int conflictMode) const;

    void navigateTo(const QString& path, bool recordHistory);
    void setCurrentPath(const QString& path);
    void startScan();
    void receiveScanBatch(std::uint64_t token, std::vector<odysea::core::Entry> entries);
    [[nodiscard]] std::size_t scanPublishInterval() const noexcept;
    // The shortest a scan will hold delivered entries on the clock alone. A
    // load that finishes inside it never reaches the rule at all, so a fast
    // local directory pays nothing for it.
    static constexpr qint64 kScanHoldFloorMilliseconds = 250;
    [[nodiscard]] static bool scanHoldExpired(qint64 holdStartMilliseconds,
                                              qint64 nowMilliseconds) noexcept;
    void drainPendingScanEntries();
    // Reads the directory. Called only once the watch over it reports itself
    // armed, so that every change to the directory is either already in what
    // this reads or is reported as an event; see startScan.
    void beginArmedScan();
    void receiveScanComplete(odysea::core::ScanSummary summary);
    void applyWatchUpdate(DirectoryWatchUpdate update);
    // Applies the watch deliveries that arrived while the listing was being
    // read, in the order they arrived. Reports whether one of them asked for
    // a rescan, rather than starting it, because starting a scan from inside
    // the drain would clear the buffer being drained.
    [[nodiscard]] bool drainHeldWatchUpdates();
    // Requests a watch over the current path and returns the token it will
    // report under.
    std::uint64_t replaceWatch();
    void applyPresentationSettings(bool finalScanBatch = false);

    // The only three ways the scanned listing may change. Each keeps the name
    // index in step with the entries, so no caller can leave a stale row
    // behind a live entry.
    //
    // The listing is indexed by name rather than by key. Every entry in it has
    // the scanned directory as its parent, and a watch delivery is discarded
    // unless it describes that same directory, so an entry's key is its name
    // prefixed by a path both sides have already agreed on. Matching on the
    // key and matching on the name therefore select the same entry, and the
    // name costs neither a path normalization nor a string allocation to
    // probe.
    void setScannedEntries(std::vector<odysea::core::Entry> entries);
    void mergeScannedEntry(odysea::core::Entry entry);
    void eraseScannedEntries(const QSet<QString>& names);
    void rebuildScannedRowIndex();

    // The only four ways the presented rows may change. Each keeps the row
    // keys and the key index in step with the entries, so no caller can leave
    // a stale key behind a live row.
    void setEntryRows(std::vector<odysea::core::Entry> entries, std::vector<QString> keys);
    void eraseEntryRows(int first, int last);
    void appendEntryRows(std::vector<odysea::core::Entry> entries, std::vector<QString> keys);
    void reorderEntryRows(const std::vector<std::size_t>& order);
    void rebuildEntryRowIndex();
    void setBusy(bool busy);
    void setErrorString(const QString& errorString);
    void setStatusMessage(const QString& statusMessage);
    void setOperationBusy(bool busy);
    void setOperationErrorString(const QString& errorString);
    void setCurrentIndex(int row);
    void remapEntryKey(const QString& oldKey, const QString& newKey);
    void replaceSelection(QSet<int> selection);
    void replaceSelectionKeys(QSet<QString> keys);
    void rebuildSelectionRows();
    void selectRangeTo(int row);
    void notifySelectionRoles();
    void requestOperation(const QString& operation);
    void startOperation(FilesystemOperationRequest request);
    void finishOperation(FilesystemOperationResult result);
    void postScanBatch(std::uint64_t token, std::vector<odysea::core::Entry> entries);
    void postScanComplete(odysea::core::ScanSummary summary);
    void postWatchUpdate(DirectoryWatchUpdate update);
    void initializeThumbnailService(odysea::core::ThumbnailProducer& producer,
                                    odysea::core::ThumbnailStore& store,
                                    odysea::core::ThumbnailServiceOptions options);
    void beginThumbnailGeneration();
    void reconcileThumbnails();
    void removeThumbnailState(const QString& key);
    void removeEvictedProviderImages(const QStringList& ids);
    void postThumbnailResult(odysea::core::ThumbnailResult result);
    void receiveThumbnailResult(odysea::core::ThumbnailResult result);

    QString path_;
    QString errorString_;
    QString filterText_;
    QString statusMessage_;
    QString operationErrorString_;
    QString currentEntryKey_;
    QString scannedPath_;
    std::vector<odysea::core::Entry> scannedEntries_;
    // The row each name occupies in the scanned listing. Derived from
    // scannedEntries_ and maintained only by the mutators above.
    QHash<QString, std::size_t> scannedRowsByName_;
    std::vector<odysea::core::Entry> scanBaselineEntries_;
    std::vector<odysea::core::Entry> scanEntries_;
    // Delivered entries a scan has not published yet. They are held here, not
    // in the scanned listing, so an unpublished batch cannot be observed as
    // presented state.
    std::vector<odysea::core::Entry> pendingScanEntries_;
    // Watch deliveries that arrived while a scan was reading the directory.
    // They describe changes the listing being assembled may predate, and the
    // completed listing replaces the presented rows outright, so applying
    // them before it is published would discard them. They are held here and
    // applied on top of the completed listing before the model reports idle.
    std::vector<DirectoryWatchUpdate> heldWatchUpdates_;
    // Counts the held deliveries that have been applied on top of a completed
    // listing. A case that makes a change during a scan reads this to
    // establish that the change really did travel that path, rather than
    // arriving late enough to take the ordinary one and prove nothing.
    std::uint64_t heldWatchUpdatesApplied_ = 0;
    // When the entries currently held began waiting, in milliseconds since
    // the scan started. Meaningful only while entries are held: the next
    // delivery to find the buffer empty overwrites it.
    qint64 scanHoldStartMilliseconds_ = 0;
    // Milliseconds since the current scan began. Held as a function rather
    // than read from the clock directly so a case can drive the hold rule
    // over a chosen timeline instead of sleeping through a real one; a rule
    // about waiting is otherwise testable only by waiting.
    std::function<qint64()> scanElapsedMilliseconds_;
    QElapsedTimer scanClock_;
    std::vector<odysea::core::Entry> entries_;
    // Row keys, parallel to entries_, plus the first row each key occupies.
    // Both are derived from entries_ and exist so a key is built once per
    // presented entry per update rather than once per comparison.
    std::vector<QString> entryKeys_;
    QHash<QString, int> entryRowsByKey_;
    // Counts key constructions. Reconciliation cost is dominated by how many
    // of these an update performs, and a count is a machine-independent way to
    // hold that shape: wall clock alone cannot separate a regression from a
    // loaded machine.
    mutable std::uint64_t entryKeyBuilds_ = 0;
    // Counts full passes over the rows that rebuild the key index. An update
    // that performs a fixed number of them is linear in the listing; one that
    // performs a pass per removal run is not, and no wall-clock bound
    // separates those two on an unloaded machine at a small enough size.
    std::uint64_t entryRowIndexBuilds_ = 0;
    QSet<int> selectedRows_;
    QSet<QString> selectedEntryKeys_;
    QHash<QString, QString> pendingEntryKeyRemaps_;
    QHash<QString, odysea::core::ThumbnailKey> requestedThumbnailKeys_;
    QHash<QString, QString> thumbnailIds_;
    QSet<QString> thumbnailLoadingKeys_;
    QSet<QString> rubberBandBaseKeys_;
    std::vector<PaneState> panes_{PaneState{}, PaneState{}};
    odysea::core::DirectoryScanner scanner_;
    DirectoryWatchService watchService_;
    QFutureWatcher<FilesystemOperationResult> operationWatcher_;
    ThumbnailImageProvider* thumbnailProvider_ = nullptr;
    EntryLauncher* entryLauncher_ = nullptr;
    std::unique_ptr<EntryLauncher> ownedEntryLauncher_;
    std::unique_ptr<odysea::core::ThumbnailProducer> ownedThumbnailProducer_;
    std::unique_ptr<odysea::core::ThumbnailStore> ownedThumbnailStore_;
    std::unique_ptr<odysea::core::ThumbnailService> thumbnailService_;
    std::atomic<bool> deliverCallbacks_{true};
    std::uint64_t thumbnailOwnerId_ = 0;
    std::uint64_t activeScanToken_ = 0;
    std::uint64_t watchToken_ = 0;
    // The watch token a scan is waiting to see armed before it reads the
    // directory. Zero when no scan is waiting.
    std::uint64_t pendingScanArmToken_ = 0;
    std::uint64_t thumbnailGeneration_ = 0;
    int sortMode_ = SortByName;
    int currentIndex_ = -1;
    QString selectionAnchorKey_;
    int activePane_ = 0;
    int paneCount_ = 1;
    bool busy_ = false;
    bool operationBusy_ = false;
    bool showHidden_ = false;
    bool rubberBandActive_ = false;
    bool watchRefreshPending_ = false;
    // Whether a scan is between being requested and having published its
    // listing. Watch deliveries are held for its duration.
    bool scanInFlight_ = false;
};
