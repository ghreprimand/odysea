// OdySea — a fast, keyboard-centric file manager.
//
// Entry point for the Qt Quick application shell. Registers the directory model
// with QML and loads the main scene. The optional first argument is the initial
// directory to display (defaults to the user's home directory).
#include <QDir>
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QStringList>
#include <QUrl>
#include <QVariant>

#include "directory_list_model.hpp"

int main(int argc, char* argv[]) {
    QGuiApplication app(argc, argv);
    QGuiApplication::setApplicationName("OdySea");
    QGuiApplication::setOrganizationName("Odyssey");

    const QStringList args = QGuiApplication::arguments();
    const QString start = args.size() > 1 ? args.at(1) : QDir::homePath();

    DirectoryListModel model;
    model.setPath(start);

    QQmlApplicationEngine engine;
    engine.setInitialProperties({{QStringLiteral("shellModel"), QVariant::fromValue(&model)}});
    engine.loadFromModule("OdySea", "Main");

    if (engine.rootObjects().isEmpty()) {
        return 1;
    }
    return app.exec();
}
