// Loading the declarative shell with a diagnostic on failure.
//
// The shell is reachable only through the OdySea QML module, so any packaging,
// install, or resource fault that drops a scene leaves the application unable to
// start. Without an explicit report that failure is invisible: the engine
// creates no root object and the process exits non-zero having written nothing
// a caller can capture. In-tree gates catch a missing scene; a shipped artifact
// does not, so the load path states what it tried to load and why it failed.
//
// The report goes straight to the standard error stream rather than through a
// logging category. Qt's default handler may route categorized output to the
// platform's system log when the stream is not a terminal, which is exactly the
// case for a packaging script, a service unit, or a captured launch — the
// situations where a startup failure most needs an explanation. A terminal
// launch sees the same line either way.
#ifndef ODYSEA_APP_SHELL_LOADER_HPP
#define ODYSEA_APP_SHELL_LOADER_HPP

#include <QString>
#include <QUrl>

class QQmlApplicationEngine;

namespace odysea::app {

/// The module the declarative shell is published under. Shared so the
/// application and the tests name the same module.
[[nodiscard]] inline QString shellModuleUri() {
    return QStringLiteral("OdySea");
}

/// The scene the application starts from.
[[nodiscard]] inline QString shellSceneType() {
    return QStringLiteral("Main");
}

/// Result of a shell load attempt.
struct ShellLoadOutcome {
    /// True when the engine created a root object.
    bool loaded = false;
    /// Empty on success; otherwise the reported diagnostic, which the loader has
    /// already written to the standard error stream.
    QString diagnostic;
};

/// Composes the report for a shell scene that could not be loaded.
///
/// `sceneUrl` is the scene the engine named when it reported the failure and may
/// be empty when it named none. `componentErrors` carries the engine's own
/// explanation when one is available and may also be empty. The result always
/// names the module and the type, so the message identifies the failure even
/// when the engine supplies nothing further.
[[nodiscard]] QString shellLoadDiagnostic(const QString& moduleUri, const QString& typeName,
                                          const QUrl& sceneUrl, const QString& componentErrors);

/// Loads `typeName` from the QML module `moduleUri` into `engine`.
///
/// On failure the diagnostic is written to the standard error stream before
/// returning, so a caller that exits non-zero still leaves the reason behind.
[[nodiscard]] ShellLoadOutcome loadShellScene(QQmlApplicationEngine& engine,
                                              const QString& moduleUri, const QString& typeName);

} // namespace odysea::app

#endif // ODYSEA_APP_SHELL_LOADER_HPP
