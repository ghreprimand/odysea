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
#include <QQmlContext>
#include <QQmlEngine>
#include <QtQuickTest>

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

QUICK_TEST_MAIN_WITH_SETUP(shell, GpuGateTestSetup)

#include "tst_gpu_gate_main.moc"
