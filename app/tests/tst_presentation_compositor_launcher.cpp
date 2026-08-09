// Real-compositor GPU-path gate for the presentation frame comparisons.
//
// The offscreen RHI launcher (tst_presentation_rhi_launcher.cpp) forces
// QT_QPA_PLATFORM=offscreen, so its frame comparisons run on a real OpenGL
// context but never touch a real compositor's frame lifecycle: first-frame
// exposure, surface configure/commit, and frame-callback pacing. Six
// frame-grabbing presentation tests failed deterministically under a real
// Wayland compositor while the offscreen RHI gate reported every frame green,
// because the condition that broke them lives on the compositor path, not
// merely on "a real GL context". Every shipped verification axis either
// avoided the real compositor or treated its absence as a pass.
//
// This launcher runs the full presentation suite on the ambient real
// compositor (Wayland preferred, then X11), with the OpenGL RHI scene graph
// forced. It reports three states a reader can tell apart at a glance:
//
//   RUN   A compositor and a usable OpenGL context were found; the suite is
//         exec'd on the real platform and its own pass or fail is the gate's.
//         Whether real frames actually materialized is decided by the suite's
//         vacuity sentinels, which fail loudly on an empty frame — the
//         launcher does not second-guess them with a weaker proxy check.
//   SKIP  No compositor is advertised, or no usable OpenGL context exists on
//         it; the launcher exits 77 (the CTest skip code) with the reason on
//         stderr, so a machine that cannot render the gate is visibly skipped
//         rather than silently satisfying it.
//   FAIL  The gate could not run while ODYSEA_REQUIRE_COMPOSITOR is set; the
//         launcher exits 1.
//
// The required-mode override closes the failure class this gate was born from:
// a skip that reads as a pass in the totals. The verifying environment sets
// ODYSEA_REQUIRE_COMPOSITOR=1 because it has a compositor, so a gate that
// cannot run there turns red instead of green. The launcher also exports
// ODYSEA_REQUIRE_GPU_FRAMES=1 into the suite, which turns the suite's own
// "software scene graph" skips into hard failures — so a run that reached the
// suite but skipped every GPU assertion on a software fallback cannot
// masquerade as success either.
//
// Why no window pre-flight: a standalone QQuickWindow grab forces a synchronous
// readback that succeeds even where the compositor withholds frame callbacks
// from a background window, while the suite's throttled grab of the same
// compositor comes back empty. A launcher-side grab therefore cannot predict
// whether the suite will get real frames; only the suite's own sentinels can,
// and they already fail loudly when it does not. The launcher checks what it
// can decide reliably — a compositor is advertised and OpenGL is usable — and
// leaves frame-reality to the sentinels.
#include <QByteArray>
#include <QGuiApplication>
#include <QOffscreenSurface>
#include <QOpenGLContext>

#include <array>
#include <cstdio>
#include <string>

#include <sys/wait.h>
#include <unistd.h>

namespace {

/// A real display server advertised by the ambient session, if any.
struct Compositor {
    QByteArray platform; ///< The QPA platform plugin: "wayland" or "xcb".
    QByteArray label;    ///< A human-readable name for the gate's log line.

    [[nodiscard]] bool isValid() const { return !platform.isEmpty(); }
};

/// Reads the ambient session without forcing offscreen. Wayland is preferred:
/// it is the reference session type and the path the previously undetected
/// failure lived on. X11 is the fallback real display server. A machine that
/// advertises neither has no compositor for the gate to run against.
Compositor detectCompositor() {
    if (qEnvironmentVariableIsSet("WAYLAND_DISPLAY") &&
        qEnvironmentVariableIsSet("XDG_RUNTIME_DIR")) {
        return {.platform = QByteArrayLiteral("wayland"), .label = QByteArrayLiteral("Wayland")};
    }
    if (qEnvironmentVariableIsSet("DISPLAY")) {
        return {.platform = QByteArrayLiteral("xcb"), .label = QByteArrayLiteral("X11")};
    }
    return {};
}

/// Probes a usable OpenGL context under the given real platform, in a forked
/// child so a platform-plugin abort — a stale display, a missing plugin, a
/// driver that aborts on connect — is caught here as "cannot run" instead of
/// crashing the gate. The parent holds no QGuiApplication, so the fork runs
/// while this process is still single-threaded. The child exits with _exit so
/// a failed driver probe never runs exit-time leak checks over allocations
/// that are not this project's to answer for.
bool usableOpenGlContext(const QByteArray& platform) {
    const pid_t pid = fork();
    if (pid < 0) {
        return false;
    }
    if (pid == 0) {
        qputenv("QT_QPA_PLATFORM", platform);
        int argc = 1;
        // A mutable, null-terminated argv the QGuiApplication contract accepts,
        // built without a C-style array so no array decays into a pointer.
        std::string arg0 = "compositor-probe";
        std::array<char*, 2> argv{arg0.data(), nullptr};
        int code = 0;
        {
            QGuiApplication probe(argc, argv.data());
            QOpenGLContext context;
            if (!context.create()) {
                code = 20;
            } else {
                QOffscreenSurface surface;
                surface.setFormat(context.format());
                surface.create();
                if (!surface.isValid() || !context.makeCurrent(&surface)) {
                    code = 21;
                } else {
                    context.doneCurrent();
                }
            }
        }
        _exit(code);
    }
    int status = 0;
    if (waitpid(pid, &status, 0) < 0) {
        return false;
    }
    return WIFEXITED(status) && WEXITSTATUS(status) == 0;
}

/// Reports that the gate could not run. When ODYSEA_REQUIRE_COMPOSITOR is set
/// the inability is a failure, not a skip: the verifying environment sets it
/// because a compositor is expected there, so a gate that cannot run turns the
/// totals red instead of reading as a pass.
[[noreturn]] void cannotRun(const char* reason) {
    if (qEnvironmentVariableIsSet("ODYSEA_REQUIRE_COMPOSITOR")) {
        const QByteArray message =
            QByteArrayLiteral("compositor-gate: FAIL -- required but could not run: ") + reason +
            "\nODYSEA_REQUIRE_COMPOSITOR is set, so an inability to run is a failure here, not a "
            "skip.\n";
        std::fputs(message.constData(), stderr);
        _exit(1);
    }
    const QByteArray message =
        QByteArrayLiteral("compositor-gate: SKIP -- could not run: ") + reason +
        "\nNo real-compositor GPU path was exercised. Set ODYSEA_REQUIRE_COMPOSITOR=1 where a "
        "compositor is expected to make this a failure instead of a skip.\n";
    std::fputs(message.constData(), stderr);
    _exit(77);
}

} // namespace

int main(int /*argc*/, char** argv) {
    const Compositor compositor = detectCompositor();
    if (!compositor.isValid()) {
        cannotRun("no compositor advertised (neither WAYLAND_DISPLAY nor "
                  "DISPLAY is set)");
    }
    if (!usableOpenGlContext(compositor.platform)) {
        cannotRun("a compositor is advertised but no usable OpenGL context "
                  "could be created on it");
    }

    const QByteArray runMessage = QByteArrayLiteral("compositor-gate: RUN on ") + compositor.label +
                                  " with the OpenGL RHI scene graph\n";
    std::fputs(runMessage.constData(), stderr);
    qputenv("QT_QPA_PLATFORM", compositor.platform);
    qputenv("QT_QUICK_BACKEND", QByteArrayLiteral("rhi"));
    qputenv("QSG_RHI_BACKEND", QByteArrayLiteral("opengl"));
    // Make a software fallback in the child a failure, not a silent skip: the
    // suite converts its own "software scene graph" skips into hard failures
    // when this is set, so a gate that ran cannot report success while every
    // GPU assertion was skipped.
    qputenv("ODYSEA_REQUIRE_GPU_FRAMES", QByteArrayLiteral("1"));
    execv(ODYSEA_PRESENTATION_BINARY, argv);
    // Reached only when the exec itself failed.
    std::fputs("compositor-gate: FAIL -- exec of the presentation suite failed\n", stderr);
    _exit(1);
}
