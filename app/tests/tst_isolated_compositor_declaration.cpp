// Gates the isolated-compositor declaration check against the ways it has
// actually been broken.
//
// This test exists because the check it covers has been wrong twice, and both
// times it was wrong in a direction that ACCEPTED the login session in use
// while reading as though it refused it. A refusal is not verifiable by
// inspection: text that says "the login session is excluded" looked equally
// true before and after the exclusion stopped working. So each way through is
// reproduced here as an executable case.
//
// The first form required a matching socket name, a runtime directory, and the
// path being a socket. The live session satisfies all three. The second form
// added a token file and a refusal to accept the login session's directory, but
// compared paths as TEXT and inspected the socket with a call that follows
// symbolic links, so:
//
//   * "/run/user/N//wayland-1" — one extra character — was accepted while
//     "/run/user/N/wayland-1" was refused, and the kernel resolves both to the
//     same live socket.
//   * A symbolic link in any writable directory pointing at the live socket was
//     accepted, because the directory examined was the link's own.
//   * A token the declarer wrote and a token the declarer exported were
//     compared against each other, which any pair of equal strings satisfies.
//
// Every one of those is a case below. The positive case matters just as much:
// a check that refuses everything would pass all the negative cases and be
// indistinguishable from a working one, which is the failure mode that put a
// vacuous gate in this tree before.
#include "isolated_compositor_declaration.hpp"

#include <QDir>
#include <QTemporaryDir>
#include <QTest>

#include <sys/file.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>

#include <algorithm>
#include <cstdlib>
#include <iterator>
#include <string>

namespace {

/// Binds a real AF_UNIX socket at the given path and keeps it open for the
/// lifetime of the object, so the checks under test see what they would see
/// beside a running compositor.
class BoundSocket {
  public:
    explicit BoundSocket(const std::string& path)
        : path_(path), descriptor_(::socket(AF_UNIX, SOCK_STREAM, 0)) {
        if (descriptor_ < 0) {
            return;
        }
        sockaddr_un address = {};
        address.sun_family = AF_UNIX;
        const size_t limit = sizeof(address.sun_path) - 1;
        const size_t copied = std::min(path.size(), limit);
        std::copy_n(path.begin(), copied, std::begin(address.sun_path));
        address.sun_path[copied] = '\0';
        // The POSIX socket interface takes sockaddr and the concrete family
        // struct is the documented way to fill it in.
        if (::bind(descriptor_, reinterpret_cast<sockaddr*>(&address), sizeof(address)) != 0) {
            ::close(descriptor_);
            descriptor_ = -1;
            return;
        }
        ::listen(descriptor_, 1);
    }
    BoundSocket(const BoundSocket&) = delete;
    BoundSocket& operator=(const BoundSocket&) = delete;
    BoundSocket(BoundSocket&&) = delete;
    BoundSocket& operator=(BoundSocket&&) = delete;
    ~BoundSocket() {
        if (descriptor_ >= 0) {
            ::close(descriptor_);
        }
        ::unlink(path_.c_str());
    }
    [[nodiscard]] bool isBound() const { return descriptor_ >= 0; }

  private:
    std::string path_;
    int descriptor_ = -1;
};

/// Holds an exclusive flock on a file for its lifetime, standing in for the
/// harness process that holds its liveness lock while a gate runs.
class HeldLock {
  public:
    // open(2) is declared variadic; the mode argument is required with
    // O_CREAT.
    explicit HeldLock(const std::string& path)
        : descriptor_(::open(path.c_str(), O_RDWR | O_CREAT | O_CLOEXEC, 0600)) {
        if (descriptor_ < 0) {
            return;
        }
        if (::flock(descriptor_, LOCK_EX | LOCK_NB) != 0) {
            ::close(descriptor_);
            descriptor_ = -1;
        }
    }
    HeldLock(const HeldLock&) = delete;
    HeldLock& operator=(const HeldLock&) = delete;
    HeldLock(HeldLock&&) = delete;
    HeldLock& operator=(HeldLock&&) = delete;
    ~HeldLock() {
        if (descriptor_ >= 0) {
            ::flock(descriptor_, LOCK_UN);
            ::close(descriptor_);
        }
    }
    [[nodiscard]] bool isHeld() const { return descriptor_ >= 0; }

  private:
    int descriptor_ = -1;
};

void writeFile(const QString& path, const QByteArray& contents) {
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write(contents);
    file.close();
}

/// A file that removes itself, for the cases that must place one outside a
/// QTemporaryDir. Nothing this test writes may outlive it.
class ScopedFile {
  public:
    ScopedFile(std::string path, const QByteArray& contents) : path_(std::move(path)) {
        QFile file(QString::fromStdString(path_));
        if (file.open(QIODevice::WriteOnly)) {
            file.write(contents);
            file.close();
            created_ = true;
        }
    }
    ScopedFile(const ScopedFile&) = delete;
    ScopedFile& operator=(const ScopedFile&) = delete;
    ScopedFile(ScopedFile&&) = delete;
    ScopedFile& operator=(ScopedFile&&) = delete;
    ~ScopedFile() {
        if (created_) {
            ::unlink(path_.c_str());
        }
    }
    [[nodiscard]] bool created() const { return created_; }

  private:
    std::string path_;
    bool created_ = false;
};

/// The login session's runtime directory, as the check computes it.
std::string sessionRuntimeDirectory() {
    return "/run/user/" + std::to_string(static_cast<unsigned long>(::getuid()));
}

} // namespace

class IsolatedCompositorDeclarationTest : public QObject {
    Q_OBJECT

  private slots:
    void acceptsARunDirectoryWithATokenAndAHeldLock();
    void refusesWhenTheLivenessLockIsFree();
    void refusesASymbolicLinkToAnotherSocket();
    void refusesAHardLinkToAnotherSocket();
    void refusesEverySpellingOfTheLoginSessionDirectory_data();
    void refusesEverySpellingOfTheLoginSessionDirectory();
    void refusesADirectoryThatIsNotTheDeclaredRunDirectory();
    void refusesAMismatchedOrAbsentToken_data();
    void refusesAMismatchedOrAbsentToken();
    void refusesWhenTheDeclarationVariablesAreAbsent_data();
    void refusesWhenTheDeclarationVariablesAreAbsent();
};

/// The positive case. Without it every refusal below is satisfied by a function
/// that returns false unconditionally, and the suite would certify a check that
/// can never accept the harness it exists to admit.
void IsolatedCompositorDeclarationTest::acceptsARunDirectoryWithATokenAndAHeldLock() {
    QTemporaryDir runDirectory;
    QVERIFY(runDirectory.isValid());
    const QString socketPath = runDirectory.filePath("wayland-1");
    const BoundSocket socket(socketPath.toStdString());
    QVERIFY(socket.isBound());
    writeFile(runDirectory.filePath(QString::fromLatin1(odysea_test::kIsolatedCompositorNonceFile)),
              QByteArrayLiteral("a-token"));
    const HeldLock lock(
        runDirectory.filePath(QString::fromLatin1(odysea_test::kIsolatedCompositorLockFile))
            .toStdString());
    QVERIFY(lock.isHeld());

    qputenv("ODYSEA_ISOLATED_COMPOSITOR_NONCE", "a-token");
    qputenv("ODYSEA_ISOLATED_COMPOSITOR_RUNDIR", runDirectory.path().toUtf8());

    QVERIFY(odysea_test::declarationNamesThisRunsCompositor(socketPath.toStdString()));
}

/// A free lock means no harness is alive. Everything else about this case is
/// exactly what a hand-built declaration can produce: a real socket, a matching
/// token, and a run directory that is not the session's. The lock is the only
/// thing separating the two, so this is the case that decides whether the token
/// is self-certifying.
void IsolatedCompositorDeclarationTest::refusesWhenTheLivenessLockIsFree() {
    QTemporaryDir runDirectory;
    QVERIFY(runDirectory.isValid());
    const QString socketPath = runDirectory.filePath("wayland-1");
    const BoundSocket socket(socketPath.toStdString());
    QVERIFY(socket.isBound());
    writeFile(runDirectory.filePath(QString::fromLatin1(odysea_test::kIsolatedCompositorNonceFile)),
              QByteArrayLiteral("a-token"));
    // The lock file exists but nobody holds it.
    writeFile(runDirectory.filePath(QString::fromLatin1(odysea_test::kIsolatedCompositorLockFile)),
              QByteArray());

    qputenv("ODYSEA_ISOLATED_COMPOSITOR_NONCE", "a-token");
    qputenv("ODYSEA_ISOLATED_COMPOSITOR_RUNDIR", runDirectory.path().toUtf8());

    QVERIFY(!odysea_test::declarationNamesThisRunsCompositor(socketPath.toStdString()));
}

/// A link in a directory the declarer owns, pointing at a socket somewhere else.
/// The directory checks all pass because the directory really is the run
/// directory; only refusing to follow the link catches this.
void IsolatedCompositorDeclarationTest::refusesASymbolicLinkToAnotherSocket() {
    QTemporaryDir elsewhere;
    QTemporaryDir runDirectory;
    QVERIFY(elsewhere.isValid());
    QVERIFY(runDirectory.isValid());

    const QString realSocketPath = elsewhere.filePath("wayland-1");
    const BoundSocket socket(realSocketPath.toStdString());
    QVERIFY(socket.isBound());

    const QString linkPath = runDirectory.filePath("wayland-1");
    QVERIFY(QFile::link(realSocketPath, linkPath));

    writeFile(runDirectory.filePath(QString::fromLatin1(odysea_test::kIsolatedCompositorNonceFile)),
              QByteArrayLiteral("a-token"));
    const HeldLock lock(
        runDirectory.filePath(QString::fromLatin1(odysea_test::kIsolatedCompositorLockFile))
            .toStdString());
    QVERIFY(lock.isHeld());

    qputenv("ODYSEA_ISOLATED_COMPOSITOR_NONCE", "a-token");
    qputenv("ODYSEA_ISOLATED_COMPOSITOR_RUNDIR", runDirectory.path().toUtf8());

    // stat() would report a socket here; lstat() reports the link.
    QVERIFY(!odysea_test::declarationNamesThisRunsCompositor(linkPath.toStdString()));
}

/// The route lstat does not close. A hard link is not a link as far as lstat is
/// concerned - it reports the socket itself, because it is the socket itself
/// under a second name - so every check that examines the declared path or its
/// directory passes. On the machine this guards, the run directory defaults to
/// the same filesystem as the login session's socket, which is what makes the
/// link possible in the first place.
///
/// EVERY OTHER CONDITION IS SATISFIED ON PURPOSE, for the reason spelled out in
/// the login-session case below: the directory really is the declared run
/// directory, the token matches, and the lock is genuinely held. The link count
/// is the only thing left that can refuse, so this case cannot pass for an
/// unrelated reason.
void IsolatedCompositorDeclarationTest::refusesAHardLinkToAnotherSocket() {
    QTemporaryDir elsewhere;
    QTemporaryDir runDirectory;
    QVERIFY(elsewhere.isValid());
    QVERIFY(runDirectory.isValid());

    const QString realSocketPath = elsewhere.filePath("wayland-1");
    const BoundSocket socket(realSocketPath.toStdString());
    QVERIFY(socket.isBound());

    const QString linkPath = runDirectory.filePath("wayland-1");
    if (::link(realSocketPath.toUtf8().constData(), linkPath.toUtf8().constData()) != 0) {
        // Both temporary directories have to share a filesystem for this to be
        // constructible. Where they do not, the route is not available and the
        // case has nothing to measure - which is reported rather than passed,
        // so it can never read as evidence the check was exercised.
        QSKIP(
            "the temporary directories are on different filesystems, so no hard link is possible");
    }

    // The premise of the case: lstat sees a socket, not a link, and it is the
    // same inode as the socket bound elsewhere.
    struct stat linkStatus = {};
    QCOMPARE(::lstat(linkPath.toUtf8().constData(), &linkStatus), 0);
    QVERIFY(S_ISSOCK(linkStatus.st_mode));
    QVERIFY(!S_ISLNK(linkStatus.st_mode));
    QVERIFY(linkStatus.st_nlink > 1);

    writeFile(runDirectory.filePath(QString::fromLatin1(odysea_test::kIsolatedCompositorNonceFile)),
              QByteArrayLiteral("a-token"));
    const HeldLock lock(
        runDirectory.filePath(QString::fromLatin1(odysea_test::kIsolatedCompositorLockFile))
            .toStdString());
    QVERIFY(lock.isHeld());

    qputenv("ODYSEA_ISOLATED_COMPOSITOR_NONCE", "a-token");
    qputenv("ODYSEA_ISOLATED_COMPOSITOR_RUNDIR", runDirectory.path().toUtf8());

    QVERIFY(!odysea_test::declarationNamesThisRunsCompositor(linkPath.toStdString()));
}

/// Every spelling names the login session's directory to the kernel, and the
/// doubled-slash form is a single accidental character away from the spelling
/// that was already refused. The declaration is given every other advantage:
/// the run directory is set to the session directory too, so only the identity
/// comparison can refuse.
void IsolatedCompositorDeclarationTest::refusesEverySpellingOfTheLoginSessionDirectory_data() {
    QTest::addColumn<QString>("spelling");
    const QString base = QString::fromStdString(sessionRuntimeDirectory());
    QTest::newRow("plain") << base + "/wayland-0";
    QTest::newRow("doubled separator") << base + "//wayland-0";
    QTest::newRow("leading doubled separator") << "/" + base + "/wayland-0";
    QTest::newRow("dot component") << base + "/./wayland-0";
    QTest::newRow("parent then back")
        << base + "/../" + QString::number(static_cast<unsigned long>(::getuid())) + "/wayland-0";
}

void IsolatedCompositorDeclarationTest::refusesEverySpellingOfTheLoginSessionDirectory() {
    QFETCH(QString, spelling);
    const std::string sessionDirectory = sessionRuntimeDirectory();
    if (!QDir(QString::fromStdString(sessionDirectory)).exists()) {
        QSKIP("no login-session runtime directory on this machine");
    }

    // EVERY OTHER CONDITION IS SATISFIED ON PURPOSE. An earlier version of this
    // case placed only a socket, and the check then refused it because the token
    // file was missing — so the case passed while proving nothing about the
    // directory rule, and reverting that rule to a string comparison left the
    // whole suite green. A case that is refused for a reason other than the one
    // it is named after is not evidence. So the token, the held lock and the
    // declared run directory are all made to agree here, and the ONLY thing
    // that can refuse is the identity of the directory itself.
    //
    // These are the only files this test places outside a temporary directory.
    // Both are uniquely named, neither is a name any compositor uses, and both
    // are removed when the case ends.
    const std::string ourSocketPath = sessionDirectory + "/wayland-0";
    const BoundSocket socket(ourSocketPath);
    if (!socket.isBound()) {
        QSKIP("could not bind a probe socket in the login-session runtime directory");
    }
    const ScopedFile token(sessionDirectory + "/" + odysea_test::kIsolatedCompositorNonceFile,
                           QByteArrayLiteral("a-token"));
    if (!token.created()) {
        QSKIP("could not place a probe token in the login-session runtime directory");
    }
    const std::string lockPath = sessionDirectory + "/" + odysea_test::kIsolatedCompositorLockFile;
    const ScopedFile lockFile(lockPath, QByteArray());
    if (!lockFile.created()) {
        QSKIP("could not place a probe lock in the login-session runtime directory");
    }
    const HeldLock lock(lockPath);
    QVERIFY(lock.isHeld());

    qputenv("ODYSEA_ISOLATED_COMPOSITOR_NONCE", "a-token");
    qputenv("ODYSEA_ISOLATED_COMPOSITOR_RUNDIR", QByteArray::fromStdString(sessionDirectory));

    QVERIFY(!odysea_test::declarationNamesThisRunsCompositor(spelling.toStdString()));
}

/// A directory that is neither the session's nor the declared run directory.
/// Without the identity comparison against the exported run directory, "not the
/// session" would be the only bar, and any writable directory would clear it.
void IsolatedCompositorDeclarationTest::refusesADirectoryThatIsNotTheDeclaredRunDirectory() {
    QTemporaryDir declaredRunDirectory;
    QTemporaryDir otherDirectory;
    QVERIFY(declaredRunDirectory.isValid());
    QVERIFY(otherDirectory.isValid());

    const QString socketPath = otherDirectory.filePath("wayland-1");
    const BoundSocket socket(socketPath.toStdString());
    QVERIFY(socket.isBound());
    writeFile(
        otherDirectory.filePath(QString::fromLatin1(odysea_test::kIsolatedCompositorNonceFile)),
        QByteArrayLiteral("a-token"));
    const HeldLock lock(
        otherDirectory.filePath(QString::fromLatin1(odysea_test::kIsolatedCompositorLockFile))
            .toStdString());
    QVERIFY(lock.isHeld());

    qputenv("ODYSEA_ISOLATED_COMPOSITOR_NONCE", "a-token");
    qputenv("ODYSEA_ISOLATED_COMPOSITOR_RUNDIR", declaredRunDirectory.path().toUtf8());

    QVERIFY(!odysea_test::declarationNamesThisRunsCompositor(socketPath.toStdString()));
}

void IsolatedCompositorDeclarationTest::refusesAMismatchedOrAbsentToken_data() {
    QTest::addColumn<bool>("writeToken");
    QTest::addColumn<QByteArray>("fileContents");
    QTest::addColumn<QByteArray>("exportedToken");
    QTest::newRow("absent token file") << false << QByteArray() << QByteArrayLiteral("a-token");
    QTest::newRow("mismatched token")
        << true << QByteArrayLiteral("other") << QByteArrayLiteral("a-token");
    QTest::newRow("empty token file") << true << QByteArray() << QByteArrayLiteral("a-token");
}

void IsolatedCompositorDeclarationTest::refusesAMismatchedOrAbsentToken() {
    QFETCH(bool, writeToken);
    QFETCH(QByteArray, fileContents);
    QFETCH(QByteArray, exportedToken);

    QTemporaryDir runDirectory;
    QVERIFY(runDirectory.isValid());
    const QString socketPath = runDirectory.filePath("wayland-1");
    const BoundSocket socket(socketPath.toStdString());
    QVERIFY(socket.isBound());
    if (writeToken) {
        writeFile(
            runDirectory.filePath(QString::fromLatin1(odysea_test::kIsolatedCompositorNonceFile)),
            fileContents);
    }
    const HeldLock lock(
        runDirectory.filePath(QString::fromLatin1(odysea_test::kIsolatedCompositorLockFile))
            .toStdString());
    QVERIFY(lock.isHeld());

    qputenv("ODYSEA_ISOLATED_COMPOSITOR_NONCE", exportedToken);
    qputenv("ODYSEA_ISOLATED_COMPOSITOR_RUNDIR", runDirectory.path().toUtf8());

    QVERIFY(!odysea_test::declarationNamesThisRunsCompositor(socketPath.toStdString()));
}

/// The state with nothing declared must be refusal, not acceptance by default.
void IsolatedCompositorDeclarationTest::refusesWhenTheDeclarationVariablesAreAbsent_data() {
    QTest::addColumn<bool>("setNonce");
    QTest::addColumn<bool>("setRunDirectory");
    QTest::newRow("neither set") << false << false;
    QTest::newRow("token only") << true << false;
    QTest::newRow("run directory only") << false << true;
}

void IsolatedCompositorDeclarationTest::refusesWhenTheDeclarationVariablesAreAbsent() {
    QFETCH(bool, setNonce);
    QFETCH(bool, setRunDirectory);

    QTemporaryDir runDirectory;
    QVERIFY(runDirectory.isValid());
    const QString socketPath = runDirectory.filePath("wayland-1");
    const BoundSocket socket(socketPath.toStdString());
    QVERIFY(socket.isBound());
    writeFile(runDirectory.filePath(QString::fromLatin1(odysea_test::kIsolatedCompositorNonceFile)),
              QByteArrayLiteral("a-token"));
    const HeldLock lock(
        runDirectory.filePath(QString::fromLatin1(odysea_test::kIsolatedCompositorLockFile))
            .toStdString());
    QVERIFY(lock.isHeld());

    if (setNonce) {
        qputenv("ODYSEA_ISOLATED_COMPOSITOR_NONCE", "a-token");
    } else {
        qunsetenv("ODYSEA_ISOLATED_COMPOSITOR_NONCE");
    }
    if (setRunDirectory) {
        qputenv("ODYSEA_ISOLATED_COMPOSITOR_RUNDIR", runDirectory.path().toUtf8());
    } else {
        qunsetenv("ODYSEA_ISOLATED_COMPOSITOR_RUNDIR");
    }

    QVERIFY(!odysea_test::declarationNamesThisRunsCompositor(socketPath.toStdString()));
}

QTEST_GUILESS_MAIN(IsolatedCompositorDeclarationTest)

#include "tst_isolated_compositor_declaration.moc"
