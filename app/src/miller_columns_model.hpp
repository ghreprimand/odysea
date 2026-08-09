// Qt adapter for Miller/columns navigation.
//
// Each live column owns exactly one DirectoryListModel, so it uses the same
// cancellable core scanner and incremental presentation path as the list and
// grid views. Descendant columns are destroyed as soon as their branch leaves
// the visible chain; there is deliberately no visited-directory cache.
#pragma once

#include <QAbstractListModel>
#include <QString>
#include <QtQml/qqmlregistration.h>

#include <cstdint>
#include <memory>
#include <vector>

class DirectoryListModel;
class EntryLauncher;

namespace odysea::app {

class MillerColumnsModel : public QAbstractListModel {
    Q_OBJECT
    QML_ELEMENT

    Q_PROPERTY(QString rootPath READ rootPath WRITE setRootPath NOTIFY rootPathChanged)
    Q_PROPERTY(QAbstractItemModel* columns READ columns CONSTANT)
    Q_PROPERTY(int columnCount READ liveColumnCount NOTIFY columnCountChanged)
    Q_PROPERTY(int activeColumn READ activeColumn NOTIFY activeColumnChanged)
    Q_PROPERTY(QObject* activeListing READ activeListing NOTIFY activeListingChanged)
    Q_PROPERTY(QString currentPath READ currentPath NOTIFY currentPathChanged)
    Q_PROPERTY(bool showHidden READ showHidden WRITE setShowHidden NOTIFY showHiddenChanged)
    Q_PROPERTY(QString filterText READ filterText WRITE setFilterText NOTIFY filterTextChanged)
    Q_PROPERTY(int sortMode READ sortMode WRITE setSortMode NOTIFY sortModeChanged)

  public:
    // QAbstractItemModel roles are intentionally unscoped integer keys.
    // NOLINTNEXTLINE(cppcoreguidelines-use-enum-class)
    enum Roles : std::uint16_t {
        ListingModelRole = Qt::UserRole + 1,
        PathRole,
        TitleRole,
        ActiveRole,
        DepthRole
    };
    Q_ENUM(Roles)

    explicit MillerColumnsModel(QObject* parent = nullptr);
    MillerColumnsModel(EntryLauncher& entryLauncher, QObject* parent = nullptr);
    ~MillerColumnsModel() override;
    Q_DISABLE_COPY_MOVE(MillerColumnsModel)

    [[nodiscard]] int rowCount(const QModelIndex& parent = {}) const override;
    [[nodiscard]] QVariant data(const QModelIndex& index, int role) const override;
    [[nodiscard]] QHash<int, QByteArray> roleNames() const override;

    [[nodiscard]] QString rootPath() const;
    [[nodiscard]] QAbstractItemModel* columns() noexcept;
    void setRootPath(const QString& path);
    [[nodiscard]] int liveColumnCount() const noexcept;
    [[nodiscard]] int activeColumn() const noexcept;
    [[nodiscard]] QObject* activeListing() const;
    [[nodiscard]] QString currentPath() const;
    [[nodiscard]] bool showHidden() const noexcept;
    void setShowHidden(bool showHidden);
    [[nodiscard]] QString filterText() const;
    void setFilterText(const QString& filterText);
    [[nodiscard]] int sortMode() const noexcept;
    void setSortMode(int sortMode);

    Q_INVOKABLE [[nodiscard]] QObject* columnModel(int column) const;
    Q_INVOKABLE [[nodiscard]] bool columnBusy(int column) const;
    Q_INVOKABLE [[nodiscard]] int columnCurrentIndex(int column) const;
    Q_INVOKABLE [[nodiscard]] int entryCount(int column) const;
    Q_INVOKABLE [[nodiscard]] QString entryName(int column, int row) const;
    Q_INVOKABLE [[nodiscard]] QString entryPath(int column, int row) const;
    Q_INVOKABLE [[nodiscard]] bool entryIsDirectory(int column, int row) const;
    Q_INVOKABLE [[nodiscard]] bool currentEntryIsDirectory() const;
    Q_INVOKABLE void select(int column, int row);
    Q_INVOKABLE void moveWithin(int delta);
    Q_INVOKABLE void moveToRow(int row);
    Q_INVOKABLE void moveAcross(int delta);
    Q_INVOKABLE void activate(int column, int row);
    Q_INVOKABLE void activateCurrent();
    Q_INVOKABLE void collapseBack();
    Q_INVOKABLE void collapseTo(int column);
    Q_INVOKABLE void setActiveColumn(int column);

  signals:
    void rootPathChanged();
    void columnCountChanged();
    void activeColumnChanged();
    void activeListingChanged();
    void currentPathChanged();
    void showHiddenChanged();
    void filterTextChanged();
    void sortModeChanged();
    void fileActivated(const QString& path);

  private:
    [[nodiscard]] DirectoryListModel* modelAt(int column) const;
    [[nodiscard]] std::unique_ptr<DirectoryListModel> makeListing() const;
    void appendColumn(const QString& path);
    void truncateAfter(int column);
    void resetColumns();
    void applySettings(DirectoryListModel& model) const;

    QString rootPath_;
    bool showHidden_ = false;
    QString filterText_;
    int sortMode_ = 0;
    int activeColumn_ = 0;

    // Declaration order is load-bearing: column models use the launcher and
    // therefore must be destroyed before the owned launcher.
    std::unique_ptr<EntryLauncher> ownedEntryLauncher_;
    EntryLauncher* entryLauncher_ = nullptr;
    std::vector<std::unique_ptr<DirectoryListModel>> columns_;
};

} // namespace odysea::app
