// OdySea — a fast, keyboard-centric file manager.
//
// Entry point for the Qt Quick application shell. Registers the directory model
// with QML and loads the main scene. The optional first argument is the initial
// directory to display (defaults to the user's home directory).
#include <QDir>
#include <QGuiApplication>
#include <QIcon>
#include <QQmlApplicationEngine>
#include <QQuickWindow>
#include <QSGRendererInterface>
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
    // Ask for destination alpha before the first QQuickWindow is created.
    // The QML shell remains opaque until the negotiated format proves the
    // request succeeded, so this request is safe on incapable platforms.
    QQuickWindow::setDefaultAlphaBuffer(true);
    QGuiApplication::setApplicationName("OdySea");
    QGuiApplication::setOrganizationName("Odyssey");
    QGuiApplication::setWindowIcon(
        QIcon::fromTheme(QStringLiteral("odysea"),
                         QIcon(QStringLiteral(":/qt/qml/OdySea/resources/icons/odysea.svg"))));

    const QStringList args = QGuiApplication::arguments();
    const QString start = args.size() > 1 ? args.at(1) : QDir::homePath();

    QQmlApplicationEngine engine;
    ThumbnailImageProvider& thumbnailProvider = installThumbnailProvider(engine);

    DirectoryListModel primaryModel(thumbnailProvider);
    DirectoryListModel secondaryModel(thumbnailProvider);
    primaryModel.setPath(start);
    secondaryModel.setPath(start);

    // Appearance and navigation preferences persist together in the per-user
    // application config location. The scene owns the state and only needs the
    // path, so no machine-local value enters a tracked fixture.
    const QString themeStorage =
        QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation) +
        QStringLiteral("/appearance.conf");

    engine.setInitialProperties(
        {{QStringLiteral("shellModel"), QVariant::fromValue(&primaryModel)},
         {QStringLiteral("secondaryShellModel"), QVariant::fromValue(&secondaryModel)},
         {QStringLiteral("themeStoragePath"), themeStorage}});
    const odysea::app::ShellLoadOutcome shell =
        odysea::app::loadShellScene(engine, odysea::app::shellModuleUri(), startupSceneType());
    if (!shell.loaded) {
        // The loader has already reported why on the standard error stream.
        return 1;
    }

    QObject* const rootObject = engine.rootObjects().constFirst();
    auto* const window = qobject_cast<QQuickWindow*>(rootObject);
    const bool alphaBufferAvailable = window != nullptr && window->format().alphaBufferSize() > 0;
    rootObject->setProperty("alphaBufferAvailable", alphaBufferAvailable);
    if (window != nullptr) {
        // The renderer API is unavailable while Main.qml is being constructed.
        // Publish it only once the scene graph exists, queued onto the shell's
        // thread so QML never receives a render-thread property update.
        QObject::connect(
            window, &QQuickWindow::sceneGraphInitialized, rootObject, [rootObject, window]() {
                const QSGRendererInterface::GraphicsApi api =
                    window->rendererInterface()->graphicsApi();
                const bool supportsWindowTransparency = api != QSGRendererInterface::Unknown &&
                                                        api != QSGRendererInterface::Software &&
                                                        api != QSGRendererInterface::Null;
                rootObject->setProperty("rendererSupportsWindowTransparency",
                                        supportsWindowTransparency);
            });
    }
    return app.exec();
}
