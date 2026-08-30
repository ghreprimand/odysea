// Raster acceptance for the installed scalable application icons.
//
// The QML validation suite proves that Qt applied the declared monitor scale,
// but its offscreen software backend cannot return a non-empty child-item grab
// at 2x. Rasterizing the shipped SVG sources at the exact device sizes here
// closes that gap without presenting a logical-layout grab as device-pixel
// evidence.
#include <QBuffer>
#include <QFile>
#include <QImage>
#include <QImageReader>
#include <QRegularExpression>
#include <QTest>

#include <algorithm>
#include <array>

namespace {

struct PixelBounds {
    int left = 0;
    int top = 0;
    int right = -1;
    int bottom = -1;
    int count = 0;
};

PixelBounds drawnBounds(const QImage& image) {
    PixelBounds bounds{.left = image.width(), .top = image.height()};
    for (int y = 0; y < image.height(); ++y) {
        for (int x = 0; x < image.width(); ++x) {
            if (qAlpha(image.pixel(x, y)) <= 8) {
                continue;
            }
            bounds.left = std::min(bounds.left, x);
            bounds.top = std::min(bounds.top, y);
            bounds.right = std::max(bounds.right, x);
            bounds.bottom = std::max(bounds.bottom, y);
            ++bounds.count;
        }
    }
    return bounds;
}

QByteArray readAsset(const QString& path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return {};
    }
    return file.readAll();
}

QImage renderSvg(const QByteArray& source, int deviceSize) {
    QBuffer buffer;
    buffer.setData(source);
    if (!buffer.open(QIODevice::ReadOnly)) {
        return {};
    }
    QImageReader reader(&buffer, "svg");
    reader.setScaledSize(QSize(deviceSize, deviceSize));
    return reader.read().convertToFormat(QImage::Format_ARGB32);
}

QByteArray withConsumerColor(QByteArray source, const QByteArray& color) {
    const qsizetype root = source.indexOf("<svg ");
    if (root < 0) {
        return {};
    }
    source.insert(root + 5, "color=\"" + color + "\" ");
    return source;
}

bool inkFollowsConsumer(const QByteArray& source, const QByteArray& color) {
    const QImage image = renderSvg(withConsumerColor(source, color), 32);
    if (image.isNull()) {
        return false;
    }

    const QColor expected(QString::fromLatin1(color));
    int compared = 0;
    for (int y = 0; y < image.height(); ++y) {
        for (int x = 0; x < image.width(); ++x) {
            const QRgb pixel = image.pixel(x, y);
            if (qAlpha(pixel) <= 24) {
                continue;
            }
            const QColor actual = QColor::fromRgba(pixel);
            if (std::abs(actual.red() - expected.red()) > 2 ||
                std::abs(actual.green() - expected.green()) > 2 ||
                std::abs(actual.blue() - expected.blue()) > 2) {
                return false;
            }
            ++compared;
        }
    }
    return compared > 24;
}

QString capture(const QByteArray& source, const QRegularExpression& expression) {
    const QRegularExpressionMatch match = expression.match(QString::fromUtf8(source));
    return match.hasMatch() ? match.captured(1) : QString();
}

} // namespace

class ApplicationIconTest : public QObject {
    Q_OBJECT

  private slots:
    void scalableAssetsRemainLegibleAtOneAndTwoTimesScale() {
        const std::array<int, 5> logicalSizes{16, 20, 24, 32, 48};
        const std::array<int, 2> scales{1, 2};
        const std::array<QString, 2> names{QStringLiteral("odysea.svg"),
                                           QStringLiteral("odysea-symbolic.svg")};

        for (const QString& name : names) {
            const bool symbolic = name.contains(QStringLiteral("symbolic"));
            const QString path = QStringLiteral(ODYSEA_ICON_SOURCE_DIR "/") + name;
            for (const int scale : scales) {
                for (const int logicalSize : logicalSizes) {
                    const int deviceSize = logicalSize * scale;
                    QImageReader reader(path);
                    reader.setScaledSize(QSize(deviceSize, deviceSize));
                    const QImage image = reader.read().convertToFormat(QImage::Format_ARGB32);
                    QVERIFY2(!image.isNull(), qPrintable(reader.errorString()));
                    QCOMPARE(image.size(), QSize(deviceSize, deviceSize));

                    const PixelBounds bounds = drawnBounds(image);
                    QVERIFY2(bounds.count > 0, qPrintable(name));
                    QVERIFY(bounds.right - bounds.left + 1 >= deviceSize * 2 / 3);
                    QVERIFY(bounds.bottom - bounds.top + 1 >= deviceSize * 2 / 3);
                    if (symbolic) {
                        QVERIFY(bounds.left > 0);
                        QVERIFY(bounds.top > 0);
                        QVERIFY(bounds.right < deviceSize - 1);
                        QVERIFY(bounds.bottom < deviceSize - 1);
                        QVERIFY(bounds.count < deviceSize * deviceSize * 62 / 100);
                    }
                }
            }
        }
    }

    void symbolicAssetInkFollowsItsConsumer() {
        const QString path = QStringLiteral(ODYSEA_ICON_SOURCE_DIR "/odysea-symbolic.svg");
        const QByteArray source = readAsset(path);
        QVERIFY2(!source.isEmpty(), qPrintable(path));
        QVERIFY(inkFollowsConsumer(source, QByteArrayLiteral("#ff0000")));
        QVERIFY(inkFollowsConsumer(source, QByteArrayLiteral("#00ff00")));

        QByteArray literalInk = source;
        QVERIFY(literalInk.replace("currentColor", "#404040") != source);
        QVERIFY(!inkFollowsConsumer(literalInk, QByteArrayLiteral("#ff0000")));
        QVERIFY(!inkFollowsConsumer(literalInk, QByteArrayLiteral("#00ff00")));
    }

    void identityGeometryMatchesEveryShippedSurface() {
        const QByteArray qml = readAsset(QStringLiteral(ODYSEA_QML_SOURCE_DIR "/VectorIcon.qml"));
        const QByteArray desktop = readAsset(QStringLiteral(ODYSEA_ICON_SOURCE_DIR "/odysea.svg"));
        const QByteArray symbolic =
            readAsset(QStringLiteral(ODYSEA_ICON_SOURCE_DIR "/odysea-symbolic.svg"));
        QVERIFY(!qml.isEmpty());
        QVERIFY(!desktop.isEmpty());
        QVERIFY(!symbolic.isEmpty());

        const QRegularExpression qmlIdentity(
            QStringLiteral(R"re(case\s+"identity":\s*return\s+"([^"]+)")re"));
        const QRegularExpression svgPath(QStringLiteral(R"re(<path\s+d="([^"]+)")re"));
        const QString qmlPath = capture(qml, qmlIdentity);
        const QString desktopPath = capture(desktop, svgPath);
        const QString symbolicPath = capture(symbolic, svgPath);
        QVERIFY(!qmlPath.isEmpty());
        QVERIFY(!desktopPath.isEmpty());
        QVERIFY(!symbolicPath.isEmpty());
        QCOMPARE(desktopPath, qmlPath);
        QCOMPARE(symbolicPath, qmlPath);
    }
};

QTEST_GUILESS_MAIN(ApplicationIconTest)

#include "tst_application_icon.moc"
