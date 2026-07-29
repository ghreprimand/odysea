// Qt bridge: exposes odysea::core directory listings and shell state to QML.
//
// Filesystem behavior stays in the framework-free core. This adapter schedules
// core scans away from the GUI thread and translates their results into Qt
// model roles and navigation state.
#pragma once

#include <QAbstractListModel>
#include <QFutureWatcher>
#include <QHash>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QVariantList>

#include <atomic>
#include <cstdint>
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
    Q_INVOKABLE QVariantList breadcrumbSegments() const;
    [[nodiscard]] QStringList selectedFileUrls() const;
    Q_INVOKABLE bool rowSelected(int row) const;
    Q_INVOKABLE bool rowIsDirectory(int row) const;
    Q_INVOKABLE bool canDropSelection(const QString& destinationDirectory) const;
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
    friend class DirectoryListModelTest;

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
    [[nodiscard]] QString entryKey(const odysea::core::Entry& entry) const;
    [[nodiscard]] QString entryIdentity(const odysea::core::Entry& entry) const;
    [[nodiscard]] int rowForEntryKey(const QString& key) const;
    [[nodiscard]] std::optional<odysea::core::ThumbnailKey>
    thumbnailKeyForEntry(const odysea::core::Entry& entry) const;
    [[nodiscard]] odysea::core::OperationOptions operationOptions(int conflictMode) const;

    void navigateTo(const QString& path, bool recordHistory);
    void setCurrentPath(const QString& path);
    void startScan();
    void receiveScanBatch(std::uint64_t token, std::vector<odysea::core::Entry> entries);
    void receiveScanComplete(odysea::core::ScanSummary summary);
    void applyWatchUpdate(DirectoryWatchUpdate update);
    void replaceWatch();
    void applyPresentationSettings(bool finalScanBatch = false);
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
    std::vector<odysea::core::Entry> scanBaselineEntries_;
    std::vector<odysea::core::Entry> scanEntries_;
    std::vector<odysea::core::Entry> entries_;
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
    std::uint64_t activeScanToken_ = 0;
    std::uint64_t watchToken_ = 0;
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
};
