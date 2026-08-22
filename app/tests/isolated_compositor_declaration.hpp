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
// WHY THE SECOND FORM WAS NOT ENOUGH EITHER. That form added a per-run token
// and a refusal to accept a socket whose directory was the login session's.
// Both were compared as STRINGS against a path nobody canonicalised, and the
// socket was inspected with a call that follows symbolic links. Three ways
// through were measured against the shipped code:
//
//   * A trailing slash. "/run/user/N/wayland-1" was refused; the same socket
//     spelled "/run/user/N//wayland-1" was accepted, as were the "/." and
//     "/../N/" spellings. One extra character — a spelling a shell or a
//     desktop configuration file produces by accident — turned the refusal off
//     while the kernel resolved every one of them to the login compositor.
//   * A symbolic link. A link in any writable directory pointing at the login
//     session's socket was accepted outright: the directory check was looking
//     at the link's parent, which is not the session's, and the socket check
//     followed the link and saw a socket.
//   * Nothing about the declaration was tied to a process. The token was
//     compared against a file the declarer could also write, so a value that
//     matches itself proves only that one person wrote both halves.
//
// So a path is never compared as text here. Directory identity is compared by
// device and inode, which every spelling of the same directory shares and no
// spelling of a different one does, and the socket itself is inspected without
// following links. Identity is what the kernel agrees with; a string is what a
// caller chose to type.
//
// WHAT IS PROVED, AND WHAT IS NOT. A declaration is accepted only when:
//
//   * ODYSEA_ISOLATED_COMPOSITOR_NONCE and ODYSEA_ISOLATED_COMPOSITOR_RUNDIR
//     are both set and non-empty.
//   * The declared socket is a socket and is not a symbolic link.
//   * The directory holding it is, by device and inode, the run directory the
//     harness exported — and is not the login session's runtime directory.
//   * That directory holds a token file, owned by this user and not a symbolic
//     link, whose contents equal the exported token exactly.
//   * The harness's liveness lock in that directory is HELD. A non-blocking
//     exclusive lock attempt must fail; if it succeeds, no live harness owns
//     the directory and the declaration is refused.
//
// The lock is the only one of these a declarer cannot produce by writing
// files, because the kernel releases it when the holding process dies by any
// means, SIGKILL included. It is checked with a non-blocking flock(2) attempt
// rather than fcntl(F_GETLK), which on Linux does not observe flock(2) locks
// at all and would report every lock free.
//
// State plainly what this does not achieve: a local user who runs their own
// proxy socket and holds a lock on their own directory can still present
// something shaped like a harness. Unforgeable proof is not available to a
// check that runs as the same user as the thing it is checking, and claiming
// otherwise is how the previous two forms of this interlock passed review. What
// IS achieved, and all that is claimed: no declaration can resolve onto the
// login session's compositor, and no accidental or hand-typed declaration
// succeeds.
#ifndef ODYSEA_TESTS_ISOLATED_COMPOSITOR_DECLARATION_HPP
#define ODYSEA_TESTS_ISOLATED_COMPOSITOR_DECLARATION_HPP

#include <fcntl.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include <cerrno>
#include <cstdlib>
#include <fstream>
#include <ios>
#include <string>

namespace odysea_test {

/// The file name the harness writes its per-run token into, inside the private
/// runtime directory it created for that run.
inline constexpr const char* kIsolatedCompositorNonceFile = "odysea-isolated-compositor.nonce";

/// The lock file the harness holds open and flock'd for the whole run. Its
/// being held is the only part of a declaration that cannot be fabricated by
/// writing files, so it is what binds a declaration to a live process.
inline constexpr const char* kIsolatedCompositorLockFile = "harness.lock";

/// A directory's identity as the kernel sees it. Two spellings of one directory
/// share it; two different directories do not, whatever they are called.
struct DirectoryIdentity {
    dev_t device = 0;
    ino_t inode = 0;
    bool valid = false;

    [[nodiscard]] bool sameAs(const DirectoryIdentity& other) const {
        return valid && other.valid && device == other.device && inode == other.inode;
    }
};

/// Resolves a directory to its device and inode. Uses stat, which follows
/// symbolic links: for a DIRECTORY that is the canonicalisation this check
/// wants, since the identity of what the path finally names is the question.
inline DirectoryIdentity identifyDirectory(const std::string& path) {
    struct stat status = {};
    if (::stat(path.c_str(), &status) != 0 || !S_ISDIR(status.st_mode)) {
        return {};
    }
    return {.device = status.st_dev, .inode = status.st_ino, .valid = true};
}

/// The directory part of a path, without trailing slashes. Returns "/" for a
/// path with no directory component other than the root, and "." for a bare
/// name. Trailing slashes on the input are dropped first, so "dir/sock/" and
/// "dir//sock" name the directory they actually sit in.
inline std::string parentDirectoryOf(const std::string& path) {
    std::string trimmed = path;
    while (trimmed.size() > 1 && trimmed.back() == '/') {
        trimmed.pop_back();
    }
    const std::string::size_type slash = trimmed.find_last_of('/');
    if (slash == std::string::npos) {
        return ".";
    }
    if (slash == 0) {
        return "/";
    }
    return trimmed.substr(0, slash);
}

/// Reads a token file's contents, bounded. Returns an empty string on any
/// failure, on a symbolic link, on a non-regular file, on a file this user does
/// not own, or on a file larger than a token can be — every one of which is a
/// refusal.
inline std::string readNonceFile(const std::string& path) {
    struct stat status = {};
    // lstat, not stat: a link here would let the token be supplied from a file
    // the declarer controls elsewhere.
    if (::lstat(path.c_str(), &status) != 0 || !S_ISREG(status.st_mode)) {
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

/// True when a live process holds the harness's liveness lock in the given
/// directory.
///
/// Proof of life is the point. A token file says someone wrote a file; a held
/// lock says a process is running right now, because the kernel drops it when
/// that process ends by any means. The test is an attempt to take the lock
/// non-blocking: failure means it is held, which is the accepting case here.
/// If the attempt unexpectedly SUCCEEDS the lock was free, so no harness owns
/// this directory — it is released immediately and the declaration refused.
///
/// fcntl(F_GETLK) is deliberately not used: on Linux it does not observe
/// flock(2) locks, so it would report this lock free in every case and turn the
/// check into one that always refuses. A check that cannot succeed is as
/// useless as one that cannot fail.
inline bool harnessLockIsHeld(const std::string& directory) {
    const std::string lockPath = directory + "/" + kIsolatedCompositorLockFile;
    struct stat status = {};
    if (::lstat(lockPath.c_str(), &status) != 0 || !S_ISREG(status.st_mode)) {
        return false;
    }
    // open(2) is declared variadic; no mode argument is needed without
    // O_CREAT.
    const int descriptor = ::open(lockPath.c_str(), O_RDONLY | O_NOFOLLOW | O_CLOEXEC);
    if (descriptor < 0) {
        return false;
    }
    const int taken = ::flock(descriptor, LOCK_EX | LOCK_NB);
    if (taken == 0) {
        // Nobody held it. Give it straight back and refuse.
        ::flock(descriptor, LOCK_UN);
        ::close(descriptor);
        return false;
    }
    const bool heldByAnother = (errno == EWOULDBLOCK);
    ::close(descriptor);
    return heldByAnother;
}

/// True when the socket at socketPath belongs to a compositor this run's
/// harness created. See the file comment for what each check rules out and for
/// what this deliberately does not claim.
inline bool declarationNamesThisRunsCompositor(const std::string& socketPath) {
    const char* const declaredNonce = std::getenv("ODYSEA_ISOLATED_COMPOSITOR_NONCE");
    if (declaredNonce == nullptr || *declaredNonce == '\0') {
        return false;
    }
    const char* const declaredRunDirectory = std::getenv("ODYSEA_ISOLATED_COMPOSITOR_RUNDIR");
    if (declaredRunDirectory == nullptr || *declaredRunDirectory == '\0') {
        return false;
    }

    // lstat, not stat: following a link here accepted a link in a writable
    // directory pointing straight at the login session's socket.
    struct stat socketStatus = {};
    if (::lstat(socketPath.c_str(), &socketStatus) != 0 || !S_ISSOCK(socketStatus.st_mode)) {
        return false;
    }

    const std::string socketDirectory = parentDirectoryOf(socketPath);
    const DirectoryIdentity socketDirectoryIdentity = identifyDirectory(socketDirectory);
    if (!socketDirectoryIdentity.valid) {
        return false;
    }

    // The login session's runtime directory is where a compositor is always
    // listening. Compared by identity, so every spelling of it is refused.
    std::string sessionRuntime = "/run/user/";
    sessionRuntime += std::to_string(static_cast<unsigned long>(::getuid()));
    if (socketDirectoryIdentity.sameAs(identifyDirectory(sessionRuntime))) {
        return false;
    }

    // The socket must sit in the very directory the harness created for this
    // run, not merely in some directory that is not the session's.
    if (!socketDirectoryIdentity.sameAs(identifyDirectory(declaredRunDirectory))) {
        return false;
    }

    const std::string noncePath = socketDirectory + "/" + kIsolatedCompositorNonceFile;
    const std::string recorded = readNonceFile(noncePath);
    if (recorded.empty() || recorded != std::string(declaredNonce)) {
        return false;
    }

    // Last, and the only check a declarer cannot satisfy by writing files.
    return harnessLockIsHeld(socketDirectory);
}

/// The refusal text both gates print when a declaration is present but does not
/// describe a compositor this run created. Kept in one place so the two gates
/// cannot drift into describing different rules.
inline constexpr const char* kUnprovenDeclarationReason =
    "the declared compositor was not proven to belong to this run. Its socket must be a socket "
    "and not a symbolic link; the directory holding it must be, by device and inode, the run "
    "directory exported as ODYSEA_ISOLATED_COMPOSITOR_RUNDIR and must not be the login "
    "session's runtime directory; that directory must hold the token exported as "
    "ODYSEA_ISOLATED_COMPOSITOR_NONCE; and the harness's liveness lock there must still be held "
    "by a running process. Setting the declaration variables by hand does not satisfy this, "
    "which is the point: run the gate through tools/isolated_compositor_gate.sh.";

} // namespace odysea_test

#endif // ODYSEA_TESTS_ISOLATED_COMPOSITOR_DECLARATION_HPP
