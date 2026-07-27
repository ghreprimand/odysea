#include "directory_list_model.hpp"

#include <system_error>

DirectoryListModel::DirectoryListModel(QObject* parent) : QAbstractListModel(parent) {}

int DirectoryListModel::rowCount(const QModelIndex& parent) const {
    if (parent.isValid()) {
        return 0;
    }
    return static_cast<int>(entries_.size());
}

QVariant DirectoryListModel::data(const QModelIndex& index, int role) const {
    if (!index.isValid() || index.row() < 0 || index.row() >= static_cast<int>(entries_.size())) {
        return {};
    }
    const odysea::core::Entry& entry = entries_[static_cast<std::size_t>(index.row())];
    switch (role) {
    case NameRole:
        return QString::fromStdString(entry.name);
    case IsDirRole:
        return entry.is_directory();
    case SizeRole:
        return QVariant::fromValue<qulonglong>(entry.size);
    default:
        return {};
    }
}

QHash<int, QByteArray> DirectoryListModel::roleNames() const {
    return {{NameRole, "name"}, {IsDirRole, "isDir"}, {SizeRole, "size"}};
}

QString DirectoryListModel::path() const {
    return path_;
}

void DirectoryListModel::setPath(const QString& path) {
    if (path_ == path) {
        return;
    }
    path_ = path;
    reload();
    emit pathChanged();
}

void DirectoryListModel::reload() {
    beginResetModel();
    std::error_code ec;
    const odysea::core::ListOptions options{.show_hidden = false};
    entries_ = odysea::core::read_directory(path_.toStdString(), options, ec);
    endResetModel();
}
