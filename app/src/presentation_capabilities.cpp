#include "presentation_capabilities.hpp"

#include <QObject>
#include <QPointer>
#include <QQuickWindow>

namespace odysea::app {

bool rendererSupportsWindowTransparency(QSGRendererInterface::GraphicsApi api) noexcept {
    return api != QSGRendererInterface::Unknown && api != QSGRendererInterface::Software &&
           api != QSGRendererInterface::Null;
}

void publishWindowPresentationCapabilities(QQuickWindow& window, QObject& shell) {
    shell.setProperty("alphaBufferAvailable", window.format().alphaBufferSize() > 0);

    // This signal may arrive from the scene-graph thread. Using `shell` as
    // the receiver delivers the property update to the QML object's thread,
    // while guarded pointers make a queued update harmless during teardown.
    const QPointer<QQuickWindow> guardedWindow(&window);
    const QPointer<QObject> guardedShell(&shell);
    QObject::connect(&window, &QQuickWindow::sceneGraphInitialized, &shell,
                     [guardedWindow, guardedShell]() {
                         if (guardedWindow == nullptr || guardedShell == nullptr) {
                             return;
                         }
                         const QSGRendererInterface::GraphicsApi api =
                             guardedWindow->rendererInterface()->graphicsApi();
                         guardedShell->setProperty("rendererSupportsWindowTransparency",
                                                   rendererSupportsWindowTransparency(api));
                     });
}

} // namespace odysea::app
