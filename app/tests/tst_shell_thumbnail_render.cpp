// End-to-end cover for the rendered thumbnail path.
//
// The unit tests either stop at the model role or start from an image already
// inside the provider. Neither notices the failure that matters most in
// practice: the model addresses the provider by a name the engine never
// registered, so every thumbnail URL resolves to nothing and the grid shows
// placeholders forever. Nothing logs an error and no unit test turns red.
//
// This test therefore loads the real scene from the shell module, decodes a
// real image file through the real producer, and requires the delegate's Image
// to reach Image.Ready. It also requires the same identifier to fail when it is
// addressed to any other provider name, so reaching Ready cannot be an accident
// of the engine resolving something else.
#include "directory_list_model.hpp"
#include "thumbnail_backend.hpp"
#include "thumbnail_image_provider.hpp"

#include <QImage>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQmlExpression>
#include <QQuickItem>
#include <QQuickWindow>
#include <QRegularExpression>
#include <QScopedPointer>
#include <QTemporaryDir>
#include <QTest>
#include <QUrl>
#include <QVariant>

#include <optional>
#include <system_error>

namespace {

/// Keeps the cache out of the run entirely, so the test measures decoding and
/// delivery rather than whatever a previous run left on disk.
class MemoryThumbnailStore final : public odysea::core::ThumbnailStore {
  public:
    std::optional<odysea::core::StoredThumbnail> load(const odysea::core::ThumbnailKey&,
                                                      std::error_code& error) override {
        error.clear();
        return std::nullopt;
    }

    void save(const odysea::core::ThumbnailKey&, const odysea::core::ThumbnailImage&,
              std::error_code& error) override {
        error.clear();
    }
};

QImage fixtureImage() {
    QImage image(48, 32, QImage::Format_RGBA8888);
    image.fill(QColor(32, 96, 160));
    return image;
}

/// Finds a named item in the visual tree.
///
/// View delegates have a visual parent but no object parent, so the object
/// hierarchy does not contain them and `findChild` never sees a realized cell.
QQuickItem* visualChild(QQuickItem* root, const QString& objectName) {
    if (root == nullptr) {
        return nullptr;
    }
    if (root->objectName() == objectName) {
        return root;
    }
    const QList<QQuickItem*> children = root->childItems();
    for (QQuickItem* child : children) {
        QQuickItem* found = visualChild(child, objectName);
        if (found != nullptr) {
            return found;
        }
    }
    return nullptr;
}

bool evaluatesTrue(QObject* target, const QString& expression) {
    QQmlExpression probe(qmlContext(target), target, expression);
    const QVariant value = probe.evaluate();
    return !probe.hasError() && value.toBool();
}

} // namespace

class ShellThumbnailRenderTest : public QObject {
    Q_OBJECT

  private slots:
    void aGridDelegateRendersADecodedThumbnail();
    void anIdentifierAddressedToAnotherProviderNameFails();
};

void ShellThumbnailRenderTest::aGridDelegateRendersADecodedThumbnail() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    QVERIFY(fixtureImage().save(directory.filePath(QStringLiteral("sample.png"))));

    QQmlEngine engine;
    ThumbnailImageProvider& provider = installThumbnailProvider(engine);

    QtThumbnailProducer producer;
    MemoryThumbnailStore store;
    DirectoryListModel model(provider, producer, store, {});
    model.setPath(directory.path());
    QTRY_COMPARE(model.rowCount(), 1);

    QQmlComponent component(&engine);
    component.loadFromModule("OdySea", "Main");
    QVERIFY2(!component.isError(), qPrintable(component.errorString()));
    const QScopedPointer<QObject> root(component.createWithInitialProperties(
        {{QStringLiteral("shellModel"), QVariant::fromValue(&model)}}));
    QVERIFY2(!root.isNull(), qPrintable(component.errorString()));

    auto* window = qobject_cast<QQuickWindow*>(root.data());
    QVERIFY(window != nullptr);
    QTRY_VERIFY(window->isVisible());
    QVERIFY(QTest::qWaitForWindowExposed(window));
    root->setProperty("gridMode", true);

    const QString cellImage = QStringLiteral("entryThumbnail-0");
    QTRY_VERIFY(visualChild(window->contentItem(), cellImage) != nullptr);
    QQuickItem* thumbnail = visualChild(window->contentItem(), cellImage);
    QVERIFY(thumbnail != nullptr);

    // The identifier is opaque, but the provider it addresses is not: the URL
    // must name the provider the engine was given.
    QTRY_VERIFY(!thumbnail->property("source").toUrl().isEmpty());
    const QUrl source = thumbnail->property("source").toUrl();
    QCOMPARE(source.scheme(), QStringLiteral("image"));
    QCOMPARE(source.host(), ThumbnailImageProvider::providerName());

    QTRY_VERIFY(evaluatesTrue(thumbnail, QStringLiteral("status === Image.Ready")));
    QVERIFY(thumbnail->property("paintedWidth").toReal() > 0.0);
    QVERIFY(thumbnail->property("paintedHeight").toReal() > 0.0);
}

void ShellThumbnailRenderTest::anIdentifierAddressedToAnotherProviderNameFails() {
    QQmlEngine engine;
    ThumbnailImageProvider& provider = installThumbnailProvider(engine);
    const QString identifier = QStringLiteral("fixture");
    const ThumbnailImageProvider::InsertResult inserted =
        provider.insert(identifier, fixtureImage());
    QVERIFY(inserted.retained);

    const auto loadImage = [&engine](const QString& url) {
        QQmlComponent component(&engine);
        component.setData(QStringLiteral("import QtQuick\nImage {\n    asynchronous: true\n"
                                         "    source: \"%1\"\n}\n")
                              .arg(url)
                              .toUtf8(),
                          QUrl(QStringLiteral("qrc:/odysea/probe.qml")));
        return component.create();
    };

    const QScopedPointer<QObject> resolved(
        loadImage(ThumbnailImageProvider::sourceUrl(identifier)));
    QVERIFY(!resolved.isNull());
    QTRY_VERIFY(evaluatesTrue(resolved.data(), QStringLiteral("status === Image.Ready")));

    QTest::ignoreMessage(QtWarningMsg,
                         QRegularExpression(QStringLiteral("Invalid image provider")));
    const QScopedPointer<QObject> misaddressed(
        loadImage(QStringLiteral("image://odysea-thumbnail-elsewhere/") + identifier));
    QVERIFY(!misaddressed.isNull());
    QTRY_VERIFY(evaluatesTrue(misaddressed.data(), QStringLiteral("status === Image.Error")));
}

QTEST_MAIN(ShellThumbnailRenderTest)

#include "tst_shell_thumbnail_render.moc"
