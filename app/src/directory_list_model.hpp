// Qt bridge: exposes odysea::core directory listings to QML as a list model.
//
// This is the boundary between the pure-C++ core and the Qt presentation layer.
// The core stays framework-free; this adapter translates it into a QObject the
// scene graph can bind to.
#pragma once

#include <QAbstractListModel>
#include <QString>
#include <vector>

#include "odysea/core/directory_model.hpp"

class DirectoryListModel : public QAbstractListModel {
    Q_OBJECT
    Q_PROPERTY(QString path READ path WRITE setPath NOTIFY pathChanged)

  public:
    enum Roles { NameRole = Qt::UserRole + 1, IsDirRole, SizeRole };

    explicit DirectoryListModel(QObject* parent = nullptr);

    [[nodiscard]] int rowCount(const QModelIndex& parent = {}) const override;
    [[nodiscard]] QVariant data(const QModelIndex& index, int role) const override;
    [[nodiscard]] QHash<int, QByteArray> roleNames() const override;

    [[nodiscard]] QString path() const;
    void setPath(const QString& path);

  signals:
    void pathChanged();

  private:
    void reload();

    QString path_;
    std::vector<odysea::core::Entry> entries_;
};
