// Cover for the shell load path and its failure report.
//
// A shell that cannot be loaded is the one fault the application cannot work
// around, and before this path existed it was invisible: the engine created no
// root object and the process exited non-zero having written nothing a caller
// could capture. Three things have to hold for the report to be worth anything,
// so all three are tested: the text has to name what was being loaded, it has to
// arrive on the standard error stream rather than wherever the platform's
// logging backend routes categorized output, and the application's own entry
// point has to produce it in a real process rather than only the function it
// calls.
#include "directory_list_model.hpp"
#include "shell_loader.hpp"
#include "theme_controller.hpp"
#include "theme_palettes.hpp"
#include "thumbnail_backend.hpp"
#include "thumbnail_image_provider.hpp"

#include <QByteArray>
#include <QColor>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QIODevice>
#include <QProcess>
#include <QProcessEnvironment>
#include <QQmlApplicationEngine>
#include <QString>
#include <QStringList>
#include <QTemporaryDir>
#include <QTest>
#include <QUrl>
#include <QVariant>

#include <unistd.h>

#include <cstdio>
#include <memory>
#include <optional>
#include <system_error>
#include <utility>

namespace {

using odysea::app::loadShellScene;
using odysea::app::shellLoadDiagnostic;
using odysea::app::ShellLoadOutcome;
using odysea::app::shellModuleUri;
using odysea::app::shellSceneType;

/// Keeps the disk cache out of the run: the load path is under test, not
/// whatever a previous run stored.
class MemoryThumbnailStore final : public odysea::core::ThumbnailStore {
  public:
    std::optional<odysea::core::StoredThumbnail> load(const odysea::core::ThumbnailKey& /*key*/,
                                                      std::error_code& error) override {
        error.clear();
        return std::nullopt;
    }

    void save(const odysea::core::ThumbnailKey& /*key*/,
              const odysea::core::ThumbnailImage& /*image*/, std::error_code& error) override {
        error.clear();
    }
};

/// Sole owner of a file descriptor.
///
/// Redirecting a stream means holding a descriptor across a scope, and a
/// descriptor leaked out of a failed test would silently affect every test after
/// it. Ownership is therefore explicit and the close happens in a destructor.
class OwnedDescriptor {
  public:
    OwnedDescriptor() = default;

    explicit OwnedDescriptor(int descriptor) : descriptor_(descriptor) {}

    OwnedDescriptor(const OwnedDescriptor&) = delete;
    OwnedDescriptor& operator=(const OwnedDescriptor&) = delete;

    OwnedDescriptor(OwnedDescriptor&& other) noexcept : descriptor_(other.release()) {}

    OwnedDescriptor& operator=(OwnedDescriptor&& other) noexcept {
        if (this != &other) {
            reset();
            descriptor_ = other.release();
        }
        return *this;
    }

    ~OwnedDescriptor() { reset(); }

    [[nodiscard]] bool isValid() const { return descriptor_ >= 0; }

    [[nodiscard]] int get() const { return descriptor_; }

    int release() { return std::exchange(descriptor_, -1); }

    void reset() {
        if (descriptor_ >= 0) {
            ::close(descriptor_);
            descriptor_ = -1;
        }
    }

  private:
    int descriptor_ = -1;
};

/// Redirects the process's standard error stream into a file for the duration of
/// a scope and hands back what was written.
///
/// The descriptor itself is redirected, not a Qt message handler, because the
/// question under test is whether the bytes reach the stream a caller captures.
/// A message handler would answer a different and weaker question. The write
/// target is a `QFile`, so the only descriptor this class closes itself is the
/// saved copy of the original stream.
class StandardErrorCapture {
  public:
    explicit StandardErrorCapture(QString path) : path_(std::move(path)), target_(path_) {
        if (!target_.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            return;
        }
        std::fflush(stderr);
        saved_ = OwnedDescriptor(::dup(STDERR_FILENO));
        if (!saved_.isValid()) {
            return;
        }
        active_ = ::dup2(target_.handle(), STDERR_FILENO) >= 0;
    }

    StandardErrorCapture(const StandardErrorCapture&) = delete;
    StandardErrorCapture& operator=(const StandardErrorCapture&) = delete;
    StandardErrorCapture(StandardErrorCapture&&) = delete;
    StandardErrorCapture& operator=(StandardErrorCapture&&) = delete;

    ~StandardErrorCapture() { restore(); }

    [[nodiscard]] bool isActive() const { return active_; }

    /// Restores the real stream and returns everything captured.
    QString take() {
        restore();
        QFile written(path_);
        if (!written.open(QIODevice::ReadOnly)) {
            return {};
        }
        return QString::fromLocal8Bit(written.readAll());
    }

  private:
    void restore() {
        // The saved descriptor is checked rather than inferred from `active_`:
        // leaving the process without a standard error stream would silence every
        // test that follows.
        if (active_ && saved_.isValid()) {
            std::fflush(stderr);
            ::dup2(saved_.get(), STDERR_FILENO);
        }
        active_ = false;
        saved_.reset();
        if (target_.isOpen()) {
            target_.close();
        }
    }

    QString path_;
    QFile target_;
    OwnedDescriptor saved_;
    bool active_ = false;
};

/// Writes a QML module whose only scene cannot compile and puts it on the
/// engine's import path.
///
/// The absent-type and absent-module cases exercise the engine's own wording but
/// never its located errors, which are the ones a broken install is most likely
/// to produce. The fixture is generated at run time rather than tracked, so a
/// deliberately invalid scene cannot reach the QML formatting and lint gates.
/// Returns the module URI, or an empty string when the fixture could not be
/// written.
QString installUncompilableModule(QQmlApplicationEngine& engine, const QString& root) {
    // Not const: the name is returned by value, and a const local blocks the
    // implicit move on the way out.
    QString moduleName = QStringLiteral("OdySeaUncompilableFixture");
    const QString moduleDirectory = QDir(root).filePath(moduleName);
    if (!QDir().mkpath(moduleDirectory)) {
        return {};
    }

    QFile scene(QDir(moduleDirectory).filePath(QStringLiteral("Broken.qml")));
    if (!scene.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        return {};
    }
    // Line 2 names a type that does not exist, so the engine reports a located
    // error rather than a runtime warning.
    scene.write(QByteArrayLiteral("import QtQuick\nNotARealTypeName {\n}\n"));
    scene.close();

    QFile manifest(QDir(moduleDirectory).filePath(QStringLiteral("qmldir")));
    if (!manifest.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        return {};
    }
    manifest.write(QStringLiteral("module %1\nBroken 1.0 Broken.qml\n").arg(moduleName).toUtf8());
    manifest.close();

    engine.addImportPath(root);
    return moduleName;
}

} // namespace

class ShellLoaderTest : public QObject {
    Q_OBJECT

  private slots:
    void theShellSceneLoadsFromTheModule();
    void theShellSceneReflectsStoredAppearanceOnLoad();
    void anAbsentSceneTypeReachesTheStandardErrorStream();
    void anAbsentModuleIsReported();
    void anUncompilableSceneReportsItsLocatedEngineErrors();
    void theReportNamesTheSceneUrlWhenTheEngineSuppliesOne();
    void theReportCarriesEngineErrorsOnOneLine();
    void theReportExplainsItselfWithoutEngineErrors();
    void theApplicationEntryPointReportsABrokenSceneAndFails();
    void theApplicationEntryPointStartsQuietlyWithAnIntactScene();
};

void ShellLoaderTest::theShellSceneLoadsFromTheModule() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());

    QtThumbnailProducer producer;
    MemoryThumbnailStore store;

    // The engine is released before the model it was given, so the scene never
    // evaluates a binding against a destroyed model on the way out.
    auto engine = std::make_unique<QQmlApplicationEngine>();
    ThumbnailImageProvider& provider = installThumbnailProvider(*engine);
    DirectoryListModel model(provider, producer, store, {});
    model.setPath(directory.path());

    engine->setInitialProperties({{QStringLiteral("shellModel"), QVariant::fromValue(&model)}});

    const ShellLoadOutcome outcome = loadShellScene(*engine, shellModuleUri(), shellSceneType());
    QVERIFY2(outcome.loaded, qPrintable(outcome.diagnostic));
    QCOMPARE(outcome.diagnostic, QString());
    QVERIFY(!engine->rootObjects().isEmpty());
    engine.reset();
}

void ShellLoaderTest::theShellSceneReflectsStoredAppearanceOnLoad() {
    // The exact startup sequence the entry point performs: pre-existing
    // settings file, initial properties, then the scene load. The pass is
    // consistency — the scene's bound surfaces must render the stored state,
    // not only hold it in the theme object. This is what breaks when the
    // storage-path load stops notifying: direct reads stay correct while
    // every binding evaluated during instantiation keeps the defaults.
    QTemporaryDir directory;
    QVERIFY(directory.isValid());

    const QString settingsPath = directory.filePath(QStringLiteral("appearance.conf"));
    QFile settings(settingsPath);
    QVERIFY(settings.open(QIODevice::WriteOnly | QIODevice::Truncate));
    settings.write("palette=odyssey-amber\n"
                   "profile=strong\n"
                   "density=comfortable\n"
                   "scale=1.5\n"
                   "dual_pane_enabled=true\n"
                   "split_ratio=0.6500\n");
    settings.close();

    QtThumbnailProducer producer;
    MemoryThumbnailStore store;

    auto engine = std::make_unique<QQmlApplicationEngine>();
    ThumbnailImageProvider& provider = installThumbnailProvider(*engine);
    DirectoryListModel model(provider, producer, store, {});
    model.setPath(directory.path());

    engine->setInitialProperties({{QStringLiteral("shellModel"), QVariant::fromValue(&model)},
                                  {QStringLiteral("themeStoragePath"), settingsPath}});

    const ShellLoadOutcome outcome = loadShellScene(*engine, shellModuleUri(), shellSceneType());
    QVERIFY2(outcome.loaded, qPrintable(outcome.diagnostic));
    QVERIFY(!engine->rootObjects().isEmpty());
    QObject* const window = engine->rootObjects().constFirst();

    auto* const theme = window->property("shellTheme").value<odysea::app::ThemeController*>();
    QVERIFY(theme != nullptr);

    // The theme object holds the stored state...
    QCOMPARE(theme->paletteId(), QStringLiteral("odyssey-amber"));
    QCOMPARE(theme->profile(), odysea::app::ThemeController::Strong);
    QCOMPARE(theme->uiScale(), 1.5);
    QCOMPARE(theme->density(), odysea::app::ThemeController::Comfortable);
    QVERIFY(theme->dualPaneEnabled());
    QCOMPARE(theme->splitRatio(), 0.65);

    // ...and the scene's bindings render it. The window ground is the bound
    // surface most visibly wrong when the load fails to notify, and the row
    // height proves the metric chain followed density and scale. The lookup
    // key is an lvalue for the same reason as in the controller: the palette
    // reference has static storage and this keeps that visible to compilers.
    const QString amberId = QStringLiteral("odyssey-amber");
    const QColor amberSheet = odysea::app::shellPalette(amberId).sheet;
    QCOMPARE(window->property("color").value<QColor>(), amberSheet);
    QCOMPARE(window->property("color").value<QColor>(), theme->background());
    QCOMPARE(window->property("rowHeight").toInt(), theme->rowHeight());
    QCOMPARE(window->property("rowHeight").toInt(), 60);
    QCOMPARE(window->property("paneCount").toInt(), 2);

    engine.reset();
}

void ShellLoaderTest::anAbsentSceneTypeReachesTheStandardErrorStream() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());

    QQmlApplicationEngine engine;
    ShellLoadOutcome outcome;
    QString captured;
    {
        StandardErrorCapture capture(directory.filePath(QStringLiteral("stderr.txt")));
        QVERIFY(capture.isActive());
        outcome = loadShellScene(engine, shellModuleUri(), QStringLiteral("NoSuchScene"));
        captured = capture.take();
    }

    QVERIFY(!outcome.loaded);
    QVERIFY2(outcome.diagnostic.contains(QStringLiteral("NoSuchScene")),
             qPrintable(outcome.diagnostic));
    QVERIFY2(outcome.diagnostic.contains(shellModuleUri()), qPrintable(outcome.diagnostic));

    // The captured stream is what a packaging script or service unit sees.
    QVERIFY2(captured.contains(outcome.diagnostic), qPrintable(captured));
    QVERIFY2(captured.contains(QStringLiteral("odysea: ")), qPrintable(captured));
    QVERIFY2(captured.endsWith(QLatin1Char('\n')), qPrintable(captured));
}

void ShellLoaderTest::anAbsentModuleIsReported() {
    QQmlApplicationEngine engine;
    const ShellLoadOutcome outcome =
        loadShellScene(engine, QStringLiteral("NoSuchModule"), shellSceneType());

    QVERIFY(!outcome.loaded);
    QVERIFY2(outcome.diagnostic.contains(QStringLiteral("NoSuchModule")),
             qPrintable(outcome.diagnostic));
    QVERIFY2(outcome.diagnostic.contains(shellSceneType()), qPrintable(outcome.diagnostic));

    // An absent module produces an error that carries no file and no line. The
    // engine's own aggregate string stands a placeholder location in its place,
    // which reads as a real position and sends a reader looking for a file that
    // was never named. The report omits the location instead.
    QVERIFY2(!outcome.diagnostic.contains(QStringLiteral(":-1")), qPrintable(outcome.diagnostic));
}

void ShellLoaderTest::anUncompilableSceneReportsItsLocatedEngineErrors() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());

    QQmlApplicationEngine engine;
    const QString moduleName = installUncompilableModule(engine, directory.path());
    QVERIFY(!moduleName.isEmpty());

    const ShellLoadOutcome outcome = loadShellScene(engine, moduleName, QStringLiteral("Broken"));
    QVERIFY(!outcome.loaded);

    // A located error carries the scene file and the line, so the report says
    // which file to look at instead of only which module failed.
    QVERIFY2(outcome.diagnostic.contains(QStringLiteral("Broken.qml:2:")),
             qPrintable(outcome.diagnostic));
    QVERIFY2(outcome.diagnostic.contains(QStringLiteral("NotARealTypeName")),
             qPrintable(outcome.diagnostic));
    QVERIFY2(outcome.diagnostic.contains(moduleName), qPrintable(outcome.diagnostic));
    QVERIFY2(!outcome.diagnostic.contains(QLatin1Char('\n')), qPrintable(outcome.diagnostic));
}

void ShellLoaderTest::theReportNamesTheSceneUrlWhenTheEngineSuppliesOne() {
    const QString withUrl =
        shellLoadDiagnostic(QStringLiteral("OdySea"), QStringLiteral("Main"),
                            QUrl(QStringLiteral("qrc:/qt/qml/OdySea/Main.qml")), QString());
    QVERIFY2(withUrl.contains(QStringLiteral("qrc:/qt/qml/OdySea/Main.qml")), qPrintable(withUrl));

    const QString withoutUrl =
        shellLoadDiagnostic(QStringLiteral("OdySea"), QStringLiteral("Main"), QUrl(), QString());
    QVERIFY2(!withoutUrl.contains(QStringLiteral("qrc:")), qPrintable(withoutUrl));
    QVERIFY2(!withoutUrl.contains(QStringLiteral("; scene")), qPrintable(withoutUrl));
}

void ShellLoaderTest::theReportCarriesEngineErrorsOnOneLine() {
    const QString report = shellLoadDiagnostic(QStringLiteral("OdySea"), QStringLiteral("Main"),
                                               QUrl(), QStringLiteral("first line\nsecond line\n"));
    QVERIFY2(report.contains(QStringLiteral("first line | second line")), qPrintable(report));
    QVERIFY2(!report.contains(QLatin1Char('\n')), qPrintable(report));
}

void ShellLoaderTest::theReportExplainsItselfWithoutEngineErrors() {
    const QString report = shellLoadDiagnostic(QStringLiteral("OdySea"), QStringLiteral("Main"),
                                               QUrl(), QStringLiteral("   \n  "));
    QVERIFY2(report.contains(QStringLiteral("not be installed")), qPrintable(report));
}

void ShellLoaderTest::theApplicationEntryPointReportsABrokenSceneAndFails() {
    // Built from the application's own entry point with a scene name the module
    // does not contain, so this measures what a user or packager sees from a
    // broken install rather than what the load function returns in-process.
    const QString program = QStringLiteral(ODYSEA_BROKEN_STARTUP_BINARY);
    QVERIFY2(QFileInfo::exists(program), qPrintable(program));

    QTemporaryDir directory;
    QVERIFY(directory.isValid());

    QProcess process;
    QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
    environment.insert(QStringLiteral("QT_QPA_PLATFORM"), QStringLiteral("offscreen"));
    // The sanitizer environment is inherited rather than relaxed: under the
    // sanitizer preset the failing entry point must reach exit status 1 with the
    // diagnostic and nothing else, which also means it must not leak on the way
    // out.
    process.setProcessEnvironment(environment);
    process.setProgram(program);
    process.setArguments({directory.path()});
    process.start();

    QVERIFY2(process.waitForStarted(30000), qPrintable(process.errorString()));
    QVERIFY2(process.waitForFinished(60000), qPrintable(process.errorString()));

    const QString standardError = QString::fromLocal8Bit(process.readAllStandardError());
    const QString standardOutput = QString::fromLocal8Bit(process.readAllStandardOutput());

    QCOMPARE(process.exitStatus(), QProcess::NormalExit);
    QCOMPARE(process.exitCode(), 1);
    QVERIFY2(!standardError.isEmpty(), "the broken entry point wrote nothing to standard error");
    QVERIFY2(standardError.contains(QStringLiteral("odysea: failed to load QML type")),
             qPrintable(standardError));
    QVERIFY2(standardError.contains(shellModuleUri()), qPrintable(standardError));
    QVERIFY2(standardOutput.isEmpty(), qPrintable(standardOutput));
}

void ShellLoaderTest::theApplicationEntryPointStartsQuietlyWithAnIntactScene() {
    // The same measurement against the shipped binary: an intact scene must not
    // produce the diagnostic, so a passing failure test cannot be an artifact of
    // the report being written unconditionally.
    const QString program = QStringLiteral(ODYSEA_STARTUP_BINARY);
    QVERIFY2(QFileInfo::exists(program), qPrintable(program));

    QTemporaryDir directory;
    QVERIFY(directory.isValid());

    QProcess process;
    QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
    environment.insert(QStringLiteral("QT_QPA_PLATFORM"), QStringLiteral("offscreen"));
    environment.insert(QStringLiteral("QT_QUICK_BACKEND"), QStringLiteral("software"));
    process.setProcessEnvironment(environment);
    process.setProgram(program);
    process.setArguments({directory.path()});
    process.start();

    QVERIFY2(process.waitForStarted(30000), qPrintable(process.errorString()));

    // Still running after the window is the pass: the shell loaded and the
    // process went on to its event loop. An early exit is the failure.
    const bool exitedEarly = process.waitForFinished(5000);
    const QString standardError = QString::fromLocal8Bit(process.readAllStandardError());
    if (!exitedEarly) {
        process.kill();
        process.waitForFinished(30000);
    }

    QVERIFY2(!exitedEarly,
             qPrintable(QStringLiteral("exit %1; %2").arg(process.exitCode()).arg(standardError)));
    QVERIFY2(!standardError.contains(QStringLiteral("failed to load QML type")),
             qPrintable(standardError));
}

QTEST_MAIN(ShellLoaderTest)

#include "tst_shell_loader.moc"
