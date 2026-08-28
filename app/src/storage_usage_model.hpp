// Qt bridge for the core recursive storage-usage scanner.
//
// The core owns traversal, accounting, cancellation, and filesystem policy.
// This adapter receives worker-thread snapshots, publishes them on the GUI
// thread, and exposes one stable model shared by the visual map and its
// accessible list equivalent.
#pragma once

#include <QAbstractListModel>
#include <QQmlEngine>
#include <QString>

#include "odysea/core/storage_usage.hpp"

#include <cstdint>
#include <mutex>
#include <optional>
#include <vector>

namespace odysea::app {

class StorageUsageModel : public QAbstractListModel {
    Q_OBJECT
    QML_ELEMENT

    Q_PROPERTY(QString rootPath READ rootPath NOTIFY rootPathChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
    Q_PROPERTY(bool cancelling READ cancelling NOTIFY stateChanged)
    Q_PROPERTY(bool cancelled READ cancelled NOTIFY stateChanged)
    Q_PROPERTY(QString errorString READ errorString NOTIFY stateChanged)
    Q_PROPERTY(int currentIndex READ currentIndex NOTIFY currentIndexChanged)
    Q_PROPERTY(bool canGoUp READ canGoUp NOTIFY rootPathChanged)
    Q_PROPERTY(qulonglong entriesVisited READ entriesVisited NOTIFY progressChanged)
    Q_PROPERTY(qulonglong apparentBytes READ apparentBytes NOTIFY progressChanged)
    Q_PROPERTY(qulonglong allocatedBytes READ allocatedBytes NOTIFY progressChanged)
    Q_PROPERTY(qulonglong fileCount READ fileCount NOTIFY progressChanged)
    Q_PROPERTY(qulonglong directoryCount READ directoryCount NOTIFY progressChanged)
    Q_PROPERTY(qulonglong unreadableDirectories READ unreadableDirectories NOTIFY progressChanged)
    Q_PROPERTY(qulonglong deduplicatedEntries READ deduplicatedEntries NOTIFY progressChanged)
    Q_PROPERTY(qulonglong skippedBoundaries READ skippedBoundaries NOTIFY progressChanged)

  public:
    // QAbstractItemModel roles are intentionally unscoped integer keys. The
    // QML type registry requires its built-in int spelling at this boundary.
    // NOLINTNEXTLINE(cppcoreguidelines-use-enum-class,performance-enum-size)
    enum Roles : int {
        NameRole = Qt::UserRole + 1,
        PathRole,
        IsDirectoryRole,
        KindLabelRole,
        ApparentBytesRole,
        AllocatedBytesRole,
        ApparentTextRole,
        AllocatedTextRole,
        FileCountRole,
        DirectoryCountRole,
        DeduplicatedEntriesRole,
        FinishedRole,
        SelectedRole
    };
    Q_ENUM(Roles)

    explicit StorageUsageModel(QObject* parent = nullptr);

    [[nodiscard]] int rowCount(const QModelIndex& parent = {}) const override;
    [[nodiscard]] QVariant data(const QModelIndex& index, int role) const override;
    [[nodiscard]] QHash<int, QByteArray> roleNames() const override;
    [[nodiscard]] Qt::ItemFlags flags(const QModelIndex& index) const override;

    [[nodiscard]] QString rootPath() const;
    [[nodiscard]] bool busy() const noexcept;
    [[nodiscard]] bool cancelling() const noexcept;
    [[nodiscard]] bool cancelled() const noexcept;
    [[nodiscard]] QString errorString() const;
    [[nodiscard]] int currentIndex() const noexcept;
    [[nodiscard]] bool canGoUp() const;
    [[nodiscard]] qulonglong entriesVisited() const noexcept;
    [[nodiscard]] qulonglong apparentBytes() const noexcept;
    [[nodiscard]] qulonglong allocatedBytes() const noexcept;
    [[nodiscard]] qulonglong fileCount() const noexcept;
    [[nodiscard]] qulonglong directoryCount() const noexcept;
    [[nodiscard]] qulonglong unreadableDirectories() const noexcept;
    [[nodiscard]] qulonglong deduplicatedEntries() const noexcept;
    [[nodiscard]] qulonglong skippedBoundaries() const noexcept;

    Q_INVOKABLE void start(const QString& path);
    Q_INVOKABLE void cancel();
    Q_INVOKABLE void selectRow(int row);
    Q_INVOKABLE void moveCursor(int delta);
    Q_INVOKABLE void activate(int row);
    Q_INVOKABLE void activateCurrent();
    Q_INVOKABLE void goUp();
    [[nodiscard]] Q_INVOKABLE bool rowSelected(int row) const noexcept;
    Q_INVOKABLE static QString formatBytes(qulonglong bytes);

  signals:
    void rootPathChanged();
    void busyChanged();
    void stateChanged();
    void progressChanged();
    void currentIndexChanged();
    void scanCancelled(const QString& path, qulonglong entriesVisited);
    void scanCompleted(const QString& path, bool cancelled);

  private:
    void applyProgress(const odysea::core::UsageProgress& progress);
    void applyCompletion(odysea::core::UsageSummary summary);
    void enqueueProgress(odysea::core::UsageProgress progress);
    void deliverPendingProgress();
    void applySnapshot(const std::vector<odysea::core::UsageChild>& children);
    void replaceRows(std::vector<odysea::core::UsageChild> children);
    void updateTotals(const odysea::core::UsageTotals& totals, std::uint64_t entriesVisited);
    void resetScanState(const QString& path);
    void setCurrentIndex(int row);
    void advanceActiveProgressRow();

    std::vector<odysea::core::UsageChild> rows_;
    odysea::core::UsageTotals totals_;
    std::mutex progressMutex_;
    std::optional<odysea::core::UsageProgress> pendingProgress_;
    QString rootPath_;
    QString errorString_;
    std::uint64_t activeToken_ = 0;
    std::uint64_t entriesVisited_ = 0;
    int currentIndex_ = -1;
    int activeProgressRow_ = -1;
    bool busy_ = false;
    bool cancelling_ = false;
    bool cancelled_ = false;
    bool progressDeliveryScheduled_ = false;
    // Declared last so its destructor joins the worker before callback state
    // and model storage begin destruction.
    odysea::core::UsageScanner scanner_;
};

} // namespace odysea::app
