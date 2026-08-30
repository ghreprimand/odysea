// Capability probe for the GPU-path presentation gate.
//
// The presentation suite's frame comparisons need a real scene-graph
// backend; forcing one unconditionally aborts the whole test process on a
// machine without a usable OpenGL context. This launcher closes that gap:
// it probes for a context first, and only when one exists does it replace
// itself with the presentation suite under the forced OpenGL RHI scene
// graph. Without a context it exits with the CTest skip code (77).
//
// The skip is never silent. Two kinds of machine cannot run this gate, and
// they are different problems that must not read the same in a log:
//
//   no display server reachable     Neither DISPLAY nor WAYLAND_DISPLAY is
//       set. The offscreen platform obtains its OpenGL context through a
//       display server, so with none advertised the gate cannot run and it
//       is nobody's fault — a bare CI shell, a headless build box. Printed
//       as a skip with that cause.
//   a display is reachable, GL is unusable     A display is advertised but
//       the context could not be created or made current — a missing or
//       broken driver, a GL that aborts on connect. This is a capability
//       problem on a machine that ought to have had one, and the reason
//       names the step that failed.
//
// The difference is exactly the difference between "this machine has no GL"
// and "this gate is broken here", which a zero-byte exit-77 erased.
//
// A verifying environment that knows offscreen GL is available sets
// ODYSEA_REQUIRE_OFFSCREEN_GL; the skip is then a hard failure instead, so a
// gate that cannot run where it was expected to turns the totals red rather
// than reading as a pass. It is deliberately distinct from the compositor
// gate's ODYSEA_REQUIRE_COMPOSITOR: a pure-Wayland verifier with no X display
// can require the compositor gate while honestly skipping this offscreen-GL
// one, and the two must not be coupled through a single switch.
#include <QByteArray>
#include <QGuiApplication>
#include <QOffscreenSurface>
#include <QOpenGLContext>

#include <cstdint>
#include <cstdio>
#include <unistd.h>

namespace {

/// The outcome of the offscreen OpenGL probe, kept distinct so the skip
/// reason can name what actually failed rather than collapsing every cause
/// into one silent exit code.
enum class GlProbe : std::uint8_t {
    Usable,        ///< A context was created and made current.
    NoContext,     ///< QOpenGLContext::create() returned false.
    NoSurface,     ///< The context created but its offscreen surface did not.
    NoMakeCurrent, ///< The surface was valid but makeCurrent() failed.
};

/// Creates and makes current an offscreen OpenGL context. Runs inside its
/// own scope so the probe application is destroyed before the suite starts.
GlProbe probeOffscreenGl(int argc, char** argv) {
    QGuiApplication probe(argc, argv);
    QOpenGLContext context;
    if (!context.create()) {
        return GlProbe::NoContext;
    }
    QOffscreenSurface surface;
    surface.setFormat(context.format());
    surface.create();
    if (!surface.isValid()) {
        return GlProbe::NoSurface;
    }
    if (!context.makeCurrent(&surface)) {
        return GlProbe::NoMakeCurrent;
    }
    context.doneCurrent();
    return GlProbe::Usable;
}

/// True when a display server is advertised in the environment. The offscreen
/// platform's OpenGL context comes from one; without any, the gate's inability
/// to run is an environment fact rather than a broken driver.
bool displayServerAdvertised() {
    return qEnvironmentVariableIsSet("DISPLAY") || qEnvironmentVariableIsSet("WAYLAND_DISPLAY");
}

/// Skips with a named cause, or fails when the verifier declared offscreen GL
/// to be required here. Never returns.
[[noreturn]] void skipOrFail(const QByteArray& cause) {
    if (qEnvironmentVariableIsSet("ODYSEA_REQUIRE_OFFSCREEN_GL")) {
        const QByteArray message =
            QByteArrayLiteral("rhi-gate: FAIL -- required but could not run: ") + cause +
            "\nODYSEA_REQUIRE_OFFSCREEN_GL is set, so an inability to create an offscreen OpenGL "
            "context is a failure here, not a skip.\n";
        std::fputs(message.constData(), stderr);
        _exit(1);
    }
    const QByteArray message =
        QByteArrayLiteral("rhi-gate: SKIP -- could not run: ") + cause +
        "\nNo offscreen OpenGL RHI path was exercised. Set ODYSEA_REQUIRE_OFFSCREEN_GL=1 where an "
        "offscreen GL context is expected to make this a failure instead of a skip.\n";
    std::fputs(message.constData(), stderr);
    // Skip without exit-time handlers: a failed driver probe can leave
    // allocations that are not this project's to answer for, and the suite
    // itself never ran.
    _exit(77);
}

} // namespace

int main(int argc, char** argv) {
    qputenv("QT_QPA_PLATFORM", QByteArrayLiteral("offscreen"));
    switch (probeOffscreenGl(argc, argv)) {
    case GlProbe::Usable:
        break;
    case GlProbe::NoContext:
        if (!displayServerAdvertised()) {
            skipOrFail(QByteArrayLiteral(
                "no display server reachable (neither DISPLAY nor WAYLAND_DISPLAY is set); the "
                "offscreen platform obtains its OpenGL context through a display server"));
        }
        skipOrFail(QByteArrayLiteral(
            "a display is reachable but QOpenGLContext::create() failed (driver or OpenGL "
            "unavailable)"));
    case GlProbe::NoSurface:
        skipOrFail(QByteArrayLiteral(
            "a display is reachable and a context was created, but its offscreen surface could "
            "not be created"));
    case GlProbe::NoMakeCurrent:
        skipOrFail(QByteArrayLiteral(
            "a display is reachable and a context was created, but makeCurrent() failed on the "
            "offscreen surface"));
    }
    qputenv("QT_QUICK_BACKEND", QByteArrayLiteral("rhi"));
    qputenv("QSG_RHI_BACKEND", QByteArrayLiteral("opengl"));
    // The probe established a usable OpenGL context. If Qt still falls back
    // to software while starting the suite, the selected GPU path was not
    // exercised and any GPU-sensitive test must fail rather than self-skip.
    qputenv("ODYSEA_REQUIRE_GPU_FRAMES", QByteArrayLiteral("1"));
    execv(ODYSEA_PRESENTATION_BINARY, argv);
    // Reached only when the exec itself failed.
    std::fputs("rhi-gate: FAIL -- exec of the presentation suite failed\n", stderr);
    return 1;
}
