// Qt bridge for the core tree indexer and fuzzy ranker.
//
// The recursive walk and matching policy remain in the Qt-free core. This
// model owns one immutable corpus per opened search surface and sends query
// changes to the off-thread ranker without touching the filesystem again.
#pragma once

#include "odysea/core/fuzzy_find.hpp"

#include <QAbstractListModel>
#include <QQmlEngine>
#include <QString>

#include <cstdint>
#include <memory>
#include <vector>

namespace odysea::app {

class FuzzyFindModel : public QAbstractListModel {
    Q_OBJECT
    QML_ELEMENT

    Q_PROPERTY(QString rootPath READ rootPath NOTIFY rootPathChanged)
    Q_PROPERTY(QString query READ query WRITE setQuery NOTIFY queryChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY stateChanged)
    Q_PROPERTY(bool ranking READ ranking NOTIFY stateChanged)
    Q_PROPERTY(QString errorString READ errorString NOTIFY stateChanged)
    Q_PROPERTY(int currentIndex READ currentIndex NOTIFY currentIndexChanged)
    Q_PROPERTY(qulonglong candidatesIndexed READ candidatesIndexed NOTIFY progressChanged)
    Q_PROPERTY(qulonglong entriesVisited READ entriesVisited NOTIFY progressChanged)
    Q_PROPERTY(qulonglong directoriesVisited READ directoriesVisited NOTIFY progressChanged)
    Q_PROPERTY(qulonglong unreadableDirectories READ unreadableDirectories NOTIFY progressChanged)
    Q_PROPERTY(qulonglong filesystemWalks READ filesystemWalks NOTIFY instrumentationChanged)
    Q_PROPERTY(qulonglong rankRequests READ rankRequests NOTIFY instrumentationChanged)
    Q_PROPERTY(qulonglong candidatesExamined READ candidatesExamined NOTIFY instrumentationChanged)
    Q_PROPERTY(
        qulonglong characterComparisons READ characterComparisons NOTIFY instrumentationChanged)

  public:
    // QAbstractItemModel roles are intentionally unscoped integer keys. The
    // QML type registry requires its built-in int spelling at this boundary.
    // NOLINTNEXTLINE(cppcoreguidelines-use-enum-class,performance-enum-size)
    enum Roles : int {
        NameRole = Qt::UserRole + 1,
        PathRole,
        RelativePathRole,
        IsDirectoryRole,
        ScoreRole,
        SelectedRole
    };
    Q_ENUM(Roles)

    explicit FuzzyFindModel(QObject* parent = nullptr);

    [[nodiscard]] int rowCount(const QModelIndex& parent = {}) const override;
    [[nodiscard]] QVariant data(const QModelIndex& index, int role) const override;
    [[nodiscard]] QHash<int, QByteArray> roleNames() const override;

    [[nodiscard]] QString rootPath() const;
    [[nodiscard]] QString query() const;
    void setQuery(const QString& query);
    [[nodiscard]] bool busy() const noexcept;
    [[nodiscard]] bool ranking() const noexcept;
    [[nodiscard]] QString errorString() const;
    [[nodiscard]] int currentIndex() const noexcept;
    [[nodiscard]] qulonglong candidatesIndexed() const noexcept;
    [[nodiscard]] qulonglong entriesVisited() const noexcept;
    [[nodiscard]] qulonglong directoriesVisited() const noexcept;
    [[nodiscard]] qulonglong unreadableDirectories() const noexcept;
    [[nodiscard]] qulonglong filesystemWalks() const noexcept;
    [[nodiscard]] qulonglong rankRequests() const noexcept;
    [[nodiscard]] qulonglong candidatesExamined() const noexcept;
    [[nodiscard]] qulonglong characterComparisons() const noexcept;

    Q_INVOKABLE void start(const QString& rootPath, bool showHidden);
    Q_INVOKABLE void cancel();
    Q_INVOKABLE void selectRow(int row);
    Q_INVOKABLE void moveCursor(int delta);
    Q_INVOKABLE void activate(int row);
    Q_INVOKABLE void activateCurrent();
    [[nodiscard]] Q_INVOKABLE bool rowSelected(int row) const noexcept;

  signals:
    void rootPathChanged();
    void queryChanged();
    void stateChanged();
    void progressChanged();
    void instrumentationChanged();
    void currentIndexChanged();
    void resultActivated(const QString& path, bool isDirectory);
    void scanCancelled(const QString& path, qulonglong entriesVisited);
    void scanCompleted(const QString& path, bool cancelled);

  private:
    void resetForRoot(const QString& rootPath);
    void resetRows();
    void startRanking();
    void applyProgress(const odysea::core::FuzzyFindProgress& progress);
    void applyScanCompletion(odysea::core::FuzzyFindSummary summary);
    void applyRankCompletion(odysea::core::FuzzyRankSummary summary);
    void setCurrentIndex(int row);

    QString rootPath_;
    QString query_;
    QString errorString_;
    std::shared_ptr<const std::vector<odysea::core::FuzzyCandidate>> corpus_;
    std::vector<odysea::core::FuzzyMatch> matches_;
    std::uint64_t activeScanToken_ = 0;
    std::uint64_t activeRankToken_ = 0;
    std::uint64_t entriesVisited_ = 0;
    std::uint64_t directoriesVisited_ = 0;
    std::uint64_t unreadableDirectories_ = 0;
    std::uint64_t filesystemWalks_ = 0;
    std::uint64_t rankRequests_ = 0;
    std::uint64_t candidatesExamined_ = 0;
    std::uint64_t characterComparisons_ = 0;
    int currentIndex_ = -1;
    bool busy_ = false;
    bool ranking_ = false;
    // Declared last so both worker destructors join before callback-visible
    // model state is destroyed.
    odysea::core::FuzzyFindScanner scanner_;
    odysea::core::FuzzyRanker ranker_;
};

} // namespace odysea::app
