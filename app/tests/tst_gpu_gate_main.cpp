// Shared QuickTest runner for the GPU-path scene suites (presentation and
// visual-validation).
//
// It is the plain QuickTest runner with two additions, each bridging an
// environment variable the GPU-path gates set into a context property the
// scenes can read:
//
//   presentationRequireGpuFrames  True when ODYSEA_REQUIRE_GPU_FRAMES is set.
//       The real-compositor gate exports it; under it the suites turn their
//       own "software scene graph" skips into hard failures, so a run that
//       reached a suite on a software fallback cannot report success while
//       every GPU assertion was skipped.
//   presentationExpectedFrameScale  The value of ODYSEA_EXPECTED_FRAME_SCALE,
//       or 0 when unset. The forced-2x validation gate exports 2 alongside
//       QT_SCALE_FACTOR=2; the device-resolution assertion then verifies the
//       grabbed frame is that multiple of the logical size, so a run that
//       came back at 1x cannot pass as if it had rendered at 2x. Every other
//       entry leaves it unset, so the property is 0 and the assertion is
//       inert.
//
// Linking the OdySea shell module makes the scenes resolve `import OdySea`
// through the module registry the application uses, so a QML file the module
// does not export fails here exactly as it would at startup.
// The runner enforces the session interlock itself, rather than relying on the
// entries that launch it. Where the suite renders is decided by CMake test
// properties and by the compositor launcher, and neither of those is part of
// this binary: running it directly is the first thing anyone does when
// investigating a compositor failure, and with QT_QPA_PLATFORM unset Qt falls
// back to the ambient session on its own. The scenes call requestActivate(), so
// that direct run could take input focus in a session someone is using and
// leave it there. A policy enforced only for the registered entries and
// documented for humans is the shape of the practice that made this necessary.
#include <QByteArray>
#include <QQmlContext>
#include <QQmlEngine>
#include <QtQuickTest>

#include <cstdio>
#include <cstdlib>

class GpuGateTestSetup : public QObject {
    Q_OBJECT

  public slots:
    void qmlEngineAvailable(QQmlEngine* engine) {
        engine->rootContext()->setContextProperty(
            "presentationRequireGpuFrames", qEnvironmentVariableIsSet("ODYSEA_REQUIRE_GPU_FRAMES"));
        bool parsed = false;
        const double expected =
            qEnvironmentVariable("ODYSEA_EXPECTED_FRAME_SCALE").toDouble(&parsed);
        engine->rootContext()->setContextProperty("presentationExpectedFrameScale",
                                                  parsed ? expected : 0.0);
    }
};

namespace {

/// Permits exactly two ways to run: rendering nowhere, or rendering on a
/// compositor a harness declared it started for this run. Anything else is an
/// ambient session, and this suite activates its window.
///
/// The declaration must name the Wayland socket and WAYLAND_DISPLAY must equal
/// it, matching the compositor launcher, so an empty marker or a harness that
/// failed to export its socket cannot authorise a run against a session in use.
/// Exits 77 so a direct run reads as a skip rather than a failure, and prints
/// the two permitted forms so the reader's next step is the safe one.
void refuseAmbientSession() {
    const QByteArray platform = qgetenv("QT_QPA_PLATFORM");
    if (platform == "offscreen" || platform == "minimal" || platform == "vnc") {
        return;
    }
    const QByteArray declaredSocket = qgetenv("ODYSEA_ISOLATED_COMPOSITOR");
    if (!declaredSocket.isEmpty() && qgetenv("WAYLAND_DISPLAY") == declaredSocket) {
        return;
    }
    std::fputs(
        "gpu-gate-suite: DECL -- declined: this suite activates its window, and neither a "
        "non-rendering platform nor a declared isolated compositor is in the environment.\n"
        "Running it against a session in use can move that session's input focus onto a test "
        "surface and leave it there.\n"
        "Run it with QT_QPA_PLATFORM=offscreen, or through the isolated-compositor harness, "
        "which sets ODYSEA_ISOLATED_COMPOSITOR to the Wayland socket it created and points "
        "WAYLAND_DISPLAY at the same socket.\n",
        stderr);
    std::_Exit(77);
}

} // namespace

int main(int argc, char** argv) {
    refuseAmbientSession();
    QTEST_SET_MAIN_SOURCE_PATH
    GpuGateTestSetup setup;
    return quick_test_main_with_setup(argc, argv, "shell", QUICK_TEST_SOURCE_DIR, &setup);
}

#include "tst_gpu_gate_main.moc"
