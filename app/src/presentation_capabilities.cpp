#include "presentation_capabilities.hpp"

#include <QObject>
#include <QPointer>
#include <QQuickWindow>

namespace odysea::app {

bool rendererPreservesWindowAlpha(QSGRendererInterface::GraphicsApi api) noexcept {
    switch (api) {
    case QSGRendererInterface::Unknown:
    case QSGRendererInterface::Software:
    case QSGRendererInterface::Null:
        return false;
    case QSGRendererInterface::OpenVG:
    case QSGRendererInterface::OpenGL:
    case QSGRendererInterface::Direct3D11:
    case QSGRendererInterface::Vulkan:
    case QSGRendererInterface::Metal:
    case QSGRendererInterface::Direct3D12:
        return true;
    }
    return false;
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
                                                   rendererPreservesWindowAlpha(api));
                     });
}

} // namespace odysea::app
