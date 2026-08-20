// Decides whether an isolated-compositor declaration describes a compositor
// this run created, rather than one that merely happens to be listening.
//
// WHY A SOCKET CHECK IS NOT ENOUGH. The gates that render an activating window
// refuse any session that was not declared as started for them. The first form
// of that interlock required three things: WAYLAND_DISPLAY equal to
// ODYSEA_ISOLATED_COMPOSITOR, a non-empty XDG_RUNTIME_DIR, and the resolved
// path being a socket. Every one of those is satisfied by the session the
// machine is already running: its Wayland socket is a socket, and setting two
// variables by hand is all it takes to authorise a focus-stealing window onto
// the surfaces someone is looking at. The check proved that A compositor was
// listening, which is exactly the one thing that is always true of a live
// session. It never proved that THIS run brought that compositor into being.
//
// WHAT IS PROVED HERE INSTEAD. The harness creates a private runtime directory
// per run, writes an unpredictable token into it, and exports that token. A
// declaration is accepted only when all of the following hold:
//
//   * ODYSEA_ISOLATED_COMPOSITOR_NONCE is set and non-empty.
//   * The directory holding the socket is not the login session's runtime
//     directory (/run/user/<uid>), where a compositor is always listening.
//   * That directory holds a token file owned by this user whose contents
//     equal the exported token exactly.
//
// A hand-set declaration fails the second and third checks: it cannot name a
// directory that is not the session's, and it cannot produce the token, which
// is read from a fresh random source per run and never written anywhere the
// declaration itself can reach. Each check is a stat or a bounded read, each
// failure is a refusal, and the default with nothing set is refusal.
//
// The token is not a secret against a hostile local user: anyone who can read
// the private directory can read it. It is not meant to be. It separates a
// declaration this harness made from one an environment inherited or a person
// typed, and that is the failure this interlock exists to stop.
#ifndef ODYSEA_TESTS_ISOLATED_COMPOSITOR_DECLARATION_HPP
#define ODYSEA_TESTS_ISOLATED_COMPOSITOR_DECLARATION_HPP

#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include <cstdlib>
#include <fstream>
#include <ios>
#include <string>

namespace odysea_test {

/// The file name the harness writes its per-run token into, inside the private
/// runtime directory it created for that run.
inline constexpr const char* kIsolatedCompositorNonceFile = "odysea-isolated-compositor.nonce";

/// The directory part of a path, without trailing slashes. Returns "/" for a
/// path with no directory component other than the root.
inline std::string parentDirectoryOf(const std::string& path) {
    const std::string::size_type slash = path.find_last_of('/');
    if (slash == std::string::npos) {
        return ".";
    }
    if (slash == 0) {
        return "/";
    }
    return path.substr(0, slash);
}

/// Reads a token file's contents, bounded. Returns an empty string on any
/// failure, on a non-regular file, on a file this user does not own, or on a
/// file larger than a token can be — every one of which is a refusal.
inline std::string readNonceFile(const std::string& path) {
    struct stat status = {};
    if (::stat(path.c_str(), &status) != 0 || !S_ISREG(status.st_mode)) {
        return {};
    }
    if (status.st_uid != ::getuid()) {
        return {};
    }
    constexpr off_t maximumTokenBytes = 512;
    if (status.st_size <= 0 || status.st_size > maximumTokenBytes) {
        return {};
    }
    std::ifstream file(path, std::ios::binary);
    if (!file.is_open()) {
        return {};
    }
    std::string contents(static_cast<std::string::size_type>(maximumTokenBytes), '\0');
    file.read(contents.data(), maximumTokenBytes);
    contents.resize(static_cast<std::string::size_type>(file.gcount()));
    while (!contents.empty() && (contents.back() == '\n' || contents.back() == '\r')) {
        contents.pop_back();
    }
    return contents;
}

/// True when the socket at socketPath belongs to a compositor this run's
/// harness created. See the file comment for what each check rules out.
inline bool declarationNamesThisRunsCompositor(const std::string& socketPath) {
    const char* const declaredNonce = std::getenv("ODYSEA_ISOLATED_COMPOSITOR_NONCE");
    if (declaredNonce == nullptr || *declaredNonce == '\0') {
        return false;
    }
    const std::string socketDirectory = parentDirectoryOf(socketPath);

    // The login session's runtime directory is where a compositor is always
    // listening, so a declaration that resolves into it proves nothing.
    std::string sessionRuntime = "/run/user/";
    sessionRuntime += std::to_string(static_cast<unsigned long>(::getuid()));
    if (socketDirectory == sessionRuntime) {
        return false;
    }

    const std::string noncePath = socketDirectory + "/" + kIsolatedCompositorNonceFile;
    const std::string recorded = readNonceFile(noncePath);
    if (recorded.empty()) {
        return false;
    }
    return recorded == std::string(declaredNonce);
}

/// The refusal text both gates print when a declaration is present but does not
/// describe a compositor this run created. Kept in one place so the two gates
/// cannot drift into describing different rules.
inline constexpr const char* kUnprovenDeclarationReason =
    "the declared compositor was not proven to belong to this run: its socket must sit in a "
    "private runtime directory (not the login session's) holding the token exported as "
    "ODYSEA_ISOLATED_COMPOSITOR_NONCE. Setting the declaration variables by hand does not "
    "satisfy this, which is the point: run the gate through tools/isolated_compositor_gate.sh.";

} // namespace odysea_test

#endif // ODYSEA_TESTS_ISOLATED_COMPOSITOR_DECLARATION_HPP
