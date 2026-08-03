// OdySea — a fast, keyboard-centric file manager.
//
// Entry point for the Qt Quick application shell. Registers the directory model
// with QML and loads the main scene. The optional first argument is the initial
// directory to display (defaults to the user's home directory).
#include <QDir>
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QStandardPaths>
#include <QStringList>
#include <QUrl>
#include <QVariant>

#include "directory_list_model.hpp"
#include "shell_loader.hpp"
#include "thumbnail_image_provider.hpp"

// The startup-failure test builds this same entry point against a scene name the
// module does not contain, so the failure branch below is measured in a real
// process rather than only in the function it calls. This is a compile-time
// choice with no runtime override: the shipped binary always compiles the
// module's real scene name.
#ifndef ODYSEA_STARTUP_SCENE_TYPE_NAME
#define ODYSEA_STARTUP_SCENE_TYPE_NAME nullptr
#endif

namespace {

/// The scene the entry point loads.
QString startupSceneType() {
    const char* const overridden = ODYSEA_STARTUP_SCENE_TYPE_NAME;
    if (overridden == nullptr) {
        return odysea::app::shellSceneType();
    }
    return QString::fromLatin1(overridden);
}

} // namespace

int main(int argc, char* argv[]) {
    QGuiApplication app(argc, argv);
    QGuiApplication::setApplicationName("OdySea");
    QGuiApplication::setOrganizationName("Odyssey");

    const QStringList args = QGuiApplication::arguments();
    const QString start = args.size() > 1 ? args.at(1) : QDir::homePath();

    QQmlApplicationEngine engine;
    ThumbnailImageProvider& thumbnailProvider = installThumbnailProvider(engine);

    DirectoryListModel model(thumbnailProvider);
    model.setPath(start);

    // Appearance and navigation preferences persist together in the per-user
    // application config location. The scene owns the state and only needs the
    // path, so no machine-local value enters a tracked fixture.
    const QString themeStorage =
        QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation) +
        QStringLiteral("/appearance.conf");

    engine.setInitialProperties({{QStringLiteral("shellModel"), QVariant::fromValue(&model)},
                                 {QStringLiteral("themeStoragePath"), themeStorage}});
    const odysea::app::ShellLoadOutcome shell =
        odysea::app::loadShellScene(engine, odysea::app::shellModuleUri(), startupSceneType());
    if (!shell.loaded) {
        // The loader has already reported why on the standard error stream.
        return 1;
    }
    return app.exec();
}
