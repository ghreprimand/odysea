#include "quick_preview_model.hpp"

#include <QFile>
#include <QImage>
#include <QSemaphore>
#include <QSignalSpy>
#include <QtConcurrentRun>
#include <QTemporaryDir>
#include <QTest>
#include <QThreadPool>

namespace {

void writeFile(const QString& path, const QByteArray& contents) {
    QFile file(path);
    QVERIFY2(file.open(QIODevice::WriteOnly), qPrintable(file.errorString()));
    QCOMPARE(file.write(contents), contents.size());
}

} // namespace

class QuickPreviewModelTest final : public QObject {
    Q_OBJECT

  private slots:
    void plainTextLoadsOffThread();
    void rasterImageIsDecodedAndBounded();
    void markdownUsesTheDocumentKind();
    void unavailableDocumentRendererIsDeclared();
    void dismissingALargeImageIsObservedByTheOwnedWorker();
};

void QuickPreviewModelTest::plainTextLoadsOffThread() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString path = directory.filePath(QStringLiteral("notes.txt"));
    writeFile(path, QByteArrayLiteral("first line\nsecond line\n"));

    QuickPreviewModel model;
    model.open(QUrl::fromLocalFile(path));

    QCOMPARE(model.state(), QuickPreviewModel::State::Loading);
    QCOMPARE(model.activeLoadCount(), 1);
    QTRY_COMPARE(model.state(), QuickPreviewModel::State::Ready);
    QCOMPARE(model.contentKind(), QuickPreviewModel::ContentKind::PlainText);
    QCOMPARE(model.text(), QStringLiteral("first line\nsecond line\n"));
    QCOMPARE(model.displayName(), QStringLiteral("notes.txt"));
    QCOMPARE(model.activeLoadCount(), 0);
}

void QuickPreviewModelTest::rasterImageIsDecodedAndBounded() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString path = directory.filePath(QStringLiteral("sample.png"));
    QImage source(QSize{2400, 1800}, QImage::Format_RGBA8888);
    source.fill(QColor{24, 96, 160});
    QVERIFY(source.save(path));

    QuickPreviewModel model;
    model.open(QUrl::fromLocalFile(path));

    QCOMPARE(model.state(), QuickPreviewModel::State::Loading);
    QTRY_COMPARE(model.state(), QuickPreviewModel::State::Ready);
    QCOMPARE(model.contentKind(), QuickPreviewModel::ContentKind::RasterImage);
    QVERIFY(!model.image().isNull());
    QVERIFY(model.image().width() <= 1600);
    QVERIFY(model.image().height() <= 1200);
}

void QuickPreviewModelTest::markdownUsesTheDocumentKind() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString path = directory.filePath(QStringLiteral("guide.md"));
    writeFile(path, QByteArrayLiteral("# Guide\n\nA rendered document.\n"));

    QuickPreviewModel model;
    model.open(QUrl::fromLocalFile(path));

    QTRY_COMPARE(model.state(), QuickPreviewModel::State::Ready);
    QCOMPARE(model.contentKind(), QuickPreviewModel::ContentKind::MarkdownDocument);
    QCOMPARE(model.text(), QStringLiteral("# Guide\n\nA rendered document.\n"));
}

void QuickPreviewModelTest::unavailableDocumentRendererIsDeclared() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString path = directory.filePath(QStringLiteral("manual.pdf"));
    writeFile(path, QByteArrayLiteral("%PDF-1.7\n% bounded fixture\n"));

    QuickPreviewModel model;
    model.open(QUrl::fromLocalFile(path));

    QTRY_COMPARE(model.state(), QuickPreviewModel::State::Unsupported);
    QVERIFY2(model.message().contains(QStringLiteral("no renderer")), qPrintable(model.message()));
    QVERIFY(model.image().isNull());
    QVERIFY(model.text().isEmpty());
}

void QuickPreviewModelTest::dismissingALargeImageIsObservedByTheOwnedWorker() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString path = directory.filePath(QStringLiteral("large.png"));
    QImage source(QSize{3200, 2400}, QImage::Format_RGBA8888);
    source.fill(QColor{72, 120, 48});
    QVERIFY(source.save(path));

    QThreadPool* const pool = QThreadPool::globalInstance();
    const int previousMaximum = pool->maxThreadCount();
    pool->setMaxThreadCount(1);
    QSemaphore blockerStarted;
    QSemaphore releaseBlocker;
    QFuture<void> blocker = QtConcurrent::run(pool, [&] {
        blockerStarted.release();
        releaseBlocker.acquire();
    });
    QVERIFY(blockerStarted.tryAcquire(1, 5000));

    QuickPreviewModel model;
    QSignalSpy cancelled(&model, &QuickPreviewModel::loadCancelled);
    model.open(QUrl::fromLocalFile(path));
    QCOMPARE(model.state(), QuickPreviewModel::State::Loading);
    model.cancel();
    QCOMPARE(model.state(), QuickPreviewModel::State::Idle);

    releaseBlocker.release();
    blocker.waitForFinished();
    QTRY_COMPARE(cancelled.count(), 1);
    QTRY_COMPARE(model.activeLoadCount(), 0);
    QCOMPARE(model.state(), QuickPreviewModel::State::Idle);
    QVERIFY(model.text().isEmpty());
    pool->setMaxThreadCount(previousMaximum);
}

QTEST_GUILESS_MAIN(QuickPreviewModelTest)

#include "tst_quick_preview_model.moc"
