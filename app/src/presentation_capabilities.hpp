// Window-presentation capability publication for the Qt Quick shell.
//
// The shell stays opaque until both destination alpha and the initialized
// scene-graph renderer prove that a transparent ground can be preserved.
#pragma once

#include <QSGRendererInterface>

class QObject;
class QQuickWindow;

namespace odysea::app {

[[nodiscard]] bool
rendererSupportsWindowTransparency(QSGRendererInterface::GraphicsApi api) noexcept;

/// Publishes the negotiated destination-alpha and renderer capabilities onto
/// the QML shell. `shell` must expose the matching boolean properties.
void publishWindowPresentationCapabilities(QQuickWindow& window, QObject& shell);

} // namespace odysea::app
