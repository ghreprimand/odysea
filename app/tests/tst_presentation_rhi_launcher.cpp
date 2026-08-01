// Capability probe for the GPU-path presentation gate.
//
// The presentation suite's frame comparisons need a real scene-graph
// backend; forcing one unconditionally aborts the whole test process on a
// machine without a usable OpenGL context. This launcher closes that gap:
// it probes for a context first, and only when one exists does it replace
// itself with the presentation suite under the forced OpenGL RHI scene
// graph. Without a context it exits with the CTest skip code (77), so the
// GPU assertions bite on every machine that can run them and skip cleanly
// on every machine that cannot.
#include <QByteArray>
#include <QGuiApplication>
#include <QOffscreenSurface>
#include <QOpenGLContext>

#include <unistd.h>

namespace {

/// Creates and makes current an offscreen OpenGL context. Runs inside its
/// own scope so the probe application is destroyed before the suite starts.
bool usableOpenGlContext(int argc, char** argv) {
    QGuiApplication probe(argc, argv);
    QOpenGLContext context;
    if (!context.create()) {
        return false;
    }
    QOffscreenSurface surface;
    surface.setFormat(context.format());
    surface.create();
    if (!surface.isValid() || !context.makeCurrent(&surface)) {
        return false;
    }
    context.doneCurrent();
    return true;
}

} // namespace

int main(int argc, char** argv) {
    qputenv("QT_QPA_PLATFORM", QByteArrayLiteral("offscreen"));
    if (!usableOpenGlContext(argc, argv)) {
        // Skip without exit-time handlers: a failed driver probe can leave
        // allocations that are not this project's to answer for, and the
        // suite itself never ran.
        _exit(77);
    }
    qputenv("QT_QUICK_BACKEND", QByteArrayLiteral("rhi"));
    qputenv("QSG_RHI_BACKEND", QByteArrayLiteral("opengl"));
    execv(ODYSEA_PRESENTATION_BINARY, argv);
    // Reached only when the exec itself failed.
    return 1;
}
