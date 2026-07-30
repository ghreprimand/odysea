#include "shell_loader.hpp"

#include <QByteArray>
#include <QList>
#include <QQmlApplicationEngine>
#include <QQmlComponent>
#include <QQmlError>
#include <QStringList>

#include <cstdio>

namespace odysea::app {
namespace {

/// Asks the engine to resolve the same module type again purely to obtain its
/// explanation.
///
/// `loadFromModule` owns its component, so the reason a type failed to resolve
/// is not otherwise reachable. The load has already failed terminally at this
/// point, so resolving once more costs nothing and turns "could not be created"
/// into the engine's own wording, such as an uninstalled module or a name that
/// is not a type. A type that resolves but fails to instantiate reports no
/// errors here; the diagnostic then rests on the module, type, and scene URL.
QString componentErrorsFor(QQmlApplicationEngine& engine, const QString& moduleUri,
                           const QString& typeName) {
    const QQmlComponent probe(&engine, moduleUri, typeName);
    if (!probe.isError()) {
        return {};
    }
    // Composed from the individual errors rather than from `errorString`, which
    // prefixes a placeholder location for an error that has none.
    QStringList descriptions;
    const QList<QQmlError> errors = probe.errors();
    descriptions.reserve(errors.size());
    for (const QQmlError& error : errors) {
        const QUrl location = error.url();
        if (!location.isEmpty() && error.line() > 0) {
            descriptions.append(QStringLiteral("%1:%2: %3")
                                    .arg(location.toString())
                                    .arg(error.line())
                                    .arg(error.description()));
        } else {
            descriptions.append(error.description());
        }
    }
    return descriptions.join(QLatin1Char('\n')).trimmed();
}

void writeToStandardError(const QString& diagnostic) {
    const QByteArray line = QStringLiteral("odysea: %1\n").arg(diagnostic).toLocal8Bit();
    std::fwrite(line.constData(), 1, static_cast<std::size_t>(line.size()), stderr);
    std::fflush(stderr);
}

} // namespace

QString shellLoadDiagnostic(const QString& moduleUri, const QString& typeName, const QUrl& sceneUrl,
                            const QString& componentErrors) {
    QString message =
        QStringLiteral("failed to load QML type %1 from module %2").arg(typeName, moduleUri);
    if (!sceneUrl.isEmpty()) {
        message += QStringLiteral("; scene %1").arg(sceneUrl.toString());
    }
    const QString errors = componentErrors.trimmed();
    if (errors.isEmpty()) {
        message += QStringLiteral(
            "; the module may be missing the scene or may not be installed with the application");
    } else {
        // Engine errors arrive one per line. Keeping them on a single line keeps
        // the report greppable in a packaging or service log.
        const QStringList lines = errors.split(QLatin1Char('\n'));
        message += QStringLiteral("; %1").arg(lines.join(QStringLiteral(" | ")));
    }
    return message;
}

ShellLoadOutcome loadShellScene(QQmlApplicationEngine& engine, const QString& moduleUri,
                                const QString& typeName) {
    QUrl failedScene;
    const QMetaObject::Connection failureReport =
        QObject::connect(&engine, &QQmlApplicationEngine::objectCreationFailed, &engine,
                         [&failedScene](const QUrl& sceneUrl) {
                             if (failedScene.isEmpty()) {
                                 failedScene = sceneUrl;
                             }
                         });

    engine.loadFromModule(moduleUri, typeName);
    QObject::disconnect(failureReport);

    if (!engine.rootObjects().isEmpty()) {
        return ShellLoadOutcome{true, QString()};
    }

    const QString diagnostic = shellLoadDiagnostic(moduleUri, typeName, failedScene,
                                                   componentErrorsFor(engine, moduleUri, typeName));
    writeToStandardError(diagnostic);
    return ShellLoadOutcome{false, diagnostic};
}

} // namespace odysea::app
