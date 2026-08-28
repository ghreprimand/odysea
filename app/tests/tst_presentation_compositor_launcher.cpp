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
// This launcher runs the full presentation suite on a real compositor (Wayland
// preferred, then X11), with the OpenGL RHI scene graph forced. It reports four
// states a reader can tell apart at a glance:
//
//   DECL  The gate declined the session it was handed, because that session
//         was not declared as one started for it, or does not match what was
//         declared. It exits 77 before forking a probe and before any window
//         can exist. See the interlock note below.
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
//
// THE SESSION INTERLOCK. This gate renders a real window on a real compositor
// and asks to be activated, which means a compositor it does not own is a
// compositor it can disturb: a window that takes focus, or that lands on an
// output no one is looking at, leaves the surfaces a person is actually using
// unable to receive keyboard or pointer input, and a run that ends abnormally
// leaves that state behind. This is not hypothetical — an interactive session
// was rendered unusable this way and had to be restarted, while the compositor
// itself, the kernel, and the drivers all remained healthy.
//
// So the gate is opt-in against a named, disposable compositor rather than
// opt-out against whatever session happens to be in the environment.
// ODYSEA_ISOLATED_COMPOSITOR carries the Wayland socket the harness created, so
// the declaration says what it authorises instead of merely that something is
// authorised. Three things follow, and each of them closes a way the earlier
// presence-only form could still have reached a session in use:
//
//   * An empty value is refused. Presence alone is what a harness exports when
//     it failed to create the socket it meant to name, which would authorise
//     precisely the run that has nothing prepared to run against.
//   * WAYLAND_DISPLAY must equal the declared socket. Otherwise a harness that
//     started a compositor and failed to export its socket would hand the gate
//     an authorisation for one session and an environment pointing at another.
//   * There is no X11 path. The declaration cannot name an X display, so an
//     inherited DISPLAY could only ever be a session the harness did not create
//     and will not tear down; DISPLAY is removed from the environment before
//     the suite is exec'd rather than merely left unpreferred.
//
// The first check comes before anything is read or forked, so declining costs
// nothing and touches nothing.
//
// ODYSEA_REQUIRE_COMPOSITOR deliberately does NOT override the interlock. That
// override exists so a machine that *cannot* run the gate goes red instead of
// reporting a skip as a pass, and this is a different thing: the gate is not
// unable, it is unwilling, and turning a refusal into a failure would only
// pressure the next reader into removing the refusal. The distinction is in the
// exit text so the two are never confused in a log. What this costs is real and
// is stated plainly: while no isolated-compositor harness exists, the real
// compositor path is unmeasured, and every result this gate produced before the
// interlock was measured against whichever session and output the window
// happened to reach.
#include "isolated_compositor_declaration.hpp"

#include <QByteArray>
#include <QGuiApplication>
#include <QOffscreenSurface>
#include <QOpenGLContext>

#include <array>
#include <cstdio>
#include <cstdlib>
#include <string>

#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

namespace {

/// A real display server advertised by the ambient session, if any.
struct Compositor {
    QByteArray platform; ///< The QPA platform plugin: "wayland" or "xcb".
    QByteArray label;    ///< A human-readable name for the gate's log line.

    [[nodiscard]] bool isValid() const { return !platform.isEmpty(); }
};

/// Resolves the session the gate is permitted to target: the one the
/// declaration names, and no other.
///
/// The declaration carries the Wayland socket the harness created, so it both
/// authorises the run and says what it authorises. WAYLAND_DISPLAY must equal
/// it — a declaration that names one socket while the environment points at
/// another describes a harness that failed to prepare the session it claims to
/// have prepared, and the gate refuses rather than rendering into whatever the
/// environment happens to hold.
///
/// There is no X11 branch here any more. A gate that may target an inherited
/// DISPLAY is a gate that may activate a window on a session in use, and the
/// declaration cannot name an X display, so an X11 fallback could only ever
/// reach a session the harness did not create.
///
/// The named socket must also exist as a socket. A declaration is a claim that
/// a compositor was started, and a claim is not evidence: a harness whose
/// compositor died, or never bound, presents exactly the same environment as
/// one that succeeded. Without this check that case reached the OpenGL probe
/// and reported an inability to run "on an advertised compositor" — a sentence
/// that is false, since nothing was listening — which is the reading the DECL
/// state exists to keep separate.
///
/// Existence is still not ownership. A live session's socket is a socket too,
/// so these checks are all satisfied by the machine's own compositor with two
/// variables set by hand. Proof that this run created the compositor is a
/// separate check, applied at the call site: see
/// isolated_compositor_declaration.hpp.
std::string declaredSocketPath(const QByteArray& declaredSocket) {
    const QByteArray runtimeDirectory = qgetenv("XDG_RUNTIME_DIR");
    if (runtimeDirectory.isEmpty()) {
        return {};
    }
    // An absolute WAYLAND_DISPLAY is a full path by the Wayland convention;
    // otherwise it names an entry inside XDG_RUNTIME_DIR.
    const QByteArray socketPath =
        declaredSocket.startsWith('/') ? declaredSocket : runtimeDirectory + '/' + declaredSocket;
    return {socketPath.constData(), static_cast<std::string::size_type>(socketPath.size())};
}

Compositor resolveDeclaredCompositor(const QByteArray& declaredSocket) {
    if (qgetenv("WAYLAND_DISPLAY") != declaredSocket) {
        return {};
    }
    const std::string socketPath = declaredSocketPath(declaredSocket);
    if (socketPath.empty()) {
        return {};
    }
    struct stat socketStatus = {};
    if (::stat(socketPath.c_str(), &socketStatus) != 0 || !S_ISSOCK(socketStatus.st_mode)) {
        return {};
    }
    return {.platform = QByteArrayLiteral("wayland"), .label = QByteArrayLiteral("Wayland")};
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

/// Reports that the gate declined the session it was handed. Always a skip,
/// never a failure: ODYSEA_REQUIRE_COMPOSITOR turns an *inability* to run red,
/// and this is a refusal to run, which is a policy the gate is enforcing rather
/// than a capability it is missing. The message says which of the two happened
/// so a log can never be read as the other.
[[noreturn]] void declineSession(const char* reason) {
    const QByteArray message =
        QByteArrayLiteral("compositor-gate: DECL -- declined: ") + reason +
        "\ncompositor-gate: REFUSAL-PROOF -- isolated-compositor interlock rejected the session "
        "before renderer setup\n"
        "\nThis gate renders an activating window and can leave a compositor it does not own "
        "with input focus on a surface nobody is looking at, so it runs only against a Wayland "
        "compositor a harness started for it and will tear down afterwards.\n"
        "ODYSEA_ISOLATED_COMPOSITOR must carry the name of that compositor's Wayland socket, "
        "and WAYLAND_DISPLAY must equal it. Setting it by hand against an interactive session "
        "defeats the interlock and is the exact case it exists to prevent.\n"
        "This is a refusal, not an inability: ODYSEA_REQUIRE_COMPOSITOR does not override it.\n";
    std::fputs(message.constData(), stderr);
    _exit(77);
}

} // namespace

int main(int /*argc*/, char** argv) {
    // First, before the environment is read for a display, before a probe is
    // forked, and before any window can exist.
    if (!qEnvironmentVariableIsSet("ODYSEA_ISOLATED_COMPOSITOR")) {
        declineSession("ODYSEA_ISOLATED_COMPOSITOR is not set, so no compositor was declared "
                       "as started for this run");
    }
    // Presence is not a declaration. An empty value is what a harness exports
    // when it failed to create the socket it meant to name, so it authorises
    // precisely the run that has nothing to run against.
    const QByteArray declaredSocket = qgetenv("ODYSEA_ISOLATED_COMPOSITOR");
    if (declaredSocket.isEmpty()) {
        declineSession("ODYSEA_ISOLATED_COMPOSITOR is set but empty, so it names no compositor; "
                       "a harness that failed to create its socket exports exactly this");
    }
    const Compositor compositor = resolveDeclaredCompositor(declaredSocket);
    if (!compositor.isValid()) {
        declineSession("the declared session is not present: WAYLAND_DISPLAY must equal the "
                       "socket named by ODYSEA_ISOLATED_COMPOSITOR, XDG_RUNTIME_DIR must be "
                       "set, and that socket must exist. A declaration is a claim that a "
                       "compositor was started, and a harness whose compositor never bound "
                       "presents the same environment as one that succeeded");
    }
    // Presence is not ownership. The checks above are all satisfied by the
    // machine's own session, whose socket is a socket and whose name can be
    // exported by hand; only the per-run token proves the compositor is one
    // this run created.
    if (!odysea_test::declarationNamesThisRunsCompositor(declaredSocketPath(declaredSocket))) {
        declineSession(odysea_test::kUnprovenDeclarationReason);
    }
    // Nothing downstream may reach an inherited X display. The declaration
    // cannot name one, so an X11 fallback could only ever be a session the
    // harness did not create and will not tear down.
    ::unsetenv("DISPLAY");
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
