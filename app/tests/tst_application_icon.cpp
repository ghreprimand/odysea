// Raster acceptance for the installed scalable application icons.
//
// The QML validation suite proves that Qt applied the declared monitor scale,
// but its offscreen software backend cannot return a non-empty child-item grab
// at 2x. Rasterizing the shipped SVG sources at the exact device sizes here
// closes that gap without presenting a logical-layout grab as device-pixel
// evidence.
#include <QImage>
#include <QImageReader>
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

    void symbolicAssetRendersAsOneNeutralInk() {
        const QString path = QStringLiteral(ODYSEA_ICON_SOURCE_DIR "/odysea-symbolic.svg");
        QImageReader reader(path);
        reader.setScaledSize(QSize(32, 32));
        const QImage image = reader.read().convertToFormat(QImage::Format_ARGB32);
        QVERIFY2(!image.isNull(), qPrintable(reader.errorString()));

        int compared = 0;
        for (int y = 0; y < image.height(); ++y) {
            for (int x = 0; x < image.width(); ++x) {
                const QRgb pixel = image.pixel(x, y);
                if (qAlpha(pixel) <= 24) {
                    continue;
                }
                QCOMPARE(qRed(pixel), qGreen(pixel));
                QCOMPARE(qGreen(pixel), qBlue(pixel));
                ++compared;
            }
        }
        QVERIFY(compared > 24);
    }
};

QTEST_GUILESS_MAIN(ApplicationIconTest)

#include "tst_application_icon.moc"
