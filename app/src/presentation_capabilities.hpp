// Window-presentation capability publication for the Qt Quick shell.
//
// The shell stays opaque until its requested destination-alpha setting and
// the initialized scene-graph renderer allow a transparent ground.
#pragma once

#include <QSGRendererInterface>

class QObject;
class QQuickWindow;

namespace odysea::app {

[[nodiscard]] bool rendererPreservesWindowAlpha(QSGRendererInterface::GraphicsApi api) noexcept;

/// Publishes the requested destination-alpha setting and renderer capability
/// onto the QML shell. `shell` must expose the matching boolean properties.
void publishWindowPresentationCapabilities(QQuickWindow& window, QObject& shell);

} // namespace odysea::app
