#include "thumbnail_backend.hpp"

#include <QImage>
#include <QTemporaryDir>
#include <QTest>

#include <filesystem>

namespace fs = std::filesystem;

namespace {

class EnvironmentRestore {
  public:
    explicit EnvironmentRestore(const char* name)
        : name_(name), existed_(qEnvironmentVariableIsSet(name)), value_(qgetenv(name)) {}

    ~EnvironmentRestore() {
        if (existed_) {
            qputenv(name_, value_);
        } else {
            qunsetenv(name_);
        }
    }

    EnvironmentRestore(const EnvironmentRestore&) = delete;
    EnvironmentRestore& operator=(const EnvironmentRestore&) = delete;

  private:
    const char* name_;
    bool existed_;
    QByteArray value_;
};

void saveImage(const fs::path& path, const char* format, const QSize& size) {
    QImage image(size, QImage::Format_RGBA8888);
    image.fill(QColorConstants::Svg::cornflowerblue);
    QVERIFY(image.save(QString::fromStdString(path.string()), format));
}

} // namespace

class ThumbnailBackendTest : public QObject {
    Q_OBJECT

  private slots:
    void producerAllowsOnlyBoundedWebFormats();
    void storeRoundTripsAndExposesStaleMetadata();
};

void ThumbnailBackendTest::producerAllowsOnlyBoundedWebFormats() {
    QTemporaryDir fixture;
    QVERIFY(fixture.isValid());
    const fs::path root = fixture.path().toStdString();
    const fs::path png = root / "source.png";
    const fs::path bmp = root / "source.bmp";
    const fs::path tooWide = root / "wide.png";
    saveImage(png, "PNG", QSize{12, 6});
    saveImage(bmp, "BMP", QSize{12, 6});
    saveImage(tooWide, "PNG", QSize{17, 1});

    QtThumbnailProducer producer(
        ThumbnailDecodeLimits{.max_dimension = 16, .max_decoded_bytes = 16UL * 16UL * 4UL});
    std::error_code error;
    const odysea::core::ThumbnailImage decoded =
        producer.produce(png, odysea::core::ThumbnailSize::Normal, error);
    QVERIFY(!error);
    QCOMPARE(decoded.width, 12U);
    QCOMPARE(decoded.height, 6U);
    QCOMPARE(decoded.byte_cost(), 12UL * 6UL * 4UL);

    static_cast<void>(producer.produce(bmp, odysea::core::ThumbnailSize::Normal, error));
    QCOMPARE(error, std::make_error_code(std::errc::operation_not_supported));

    static_cast<void>(producer.produce(tooWide, odysea::core::ThumbnailSize::Normal, error));
    QCOMPARE(error, std::make_error_code(std::errc::value_too_large));

    QtThumbnailProducer byteBounded(
        ThumbnailDecodeLimits{.max_dimension = 64, .max_decoded_bytes = 64});
    static_cast<void>(byteBounded.produce(png, odysea::core::ThumbnailSize::Normal, error));
    QCOMPARE(error, std::make_error_code(std::errc::value_too_large));
}

void ThumbnailBackendTest::storeRoundTripsAndExposesStaleMetadata() {
    QTemporaryDir fixture;
    QVERIFY(fixture.isValid());
    EnvironmentRestore restoreCache("XDG_CACHE_HOME");
    qputenv("XDG_CACHE_HOME", fixture.path().toUtf8());

    QImage source(QSize{8, 4}, QImage::Format_RGBA8888);
    source.fill(QColorConstants::Svg::darkorange);
    std::error_code error;
    const odysea::core::ThumbnailImage image = qImageToThumbnailImage(source, error);
    QVERIFY(!error);

    const odysea::core::ThumbnailKey key{.uri = "file:///sample/picture.png",
                                         .modified_seconds = 1'234,
                                         .size = 4'096,
                                         .edge = odysea::core::ThumbnailSize::Normal};
    FreedesktopThumbnailStore store;
    store.save(key, image, error);
    QVERIFY(!error);

    const std::optional<odysea::core::StoredThumbnail> loaded = store.load(key, error);
    QVERIFY(!error);
    QVERIFY(loaded.has_value());
    const odysea::core::StoredThumbnail loadedValue = loaded.value_or({});
    QCOMPARE(loadedValue.uri, key.uri);
    QCOMPARE(loadedValue.modified_seconds, key.modified_seconds);
    QCOMPARE(loadedValue.size, key.size);
    QVERIFY(loadedValue.size_recorded);
    QVERIFY(odysea::core::thumbnail_matches(loadedValue, key));
    QCOMPARE(loadedValue.image.width, image.width);
    QCOMPARE(loadedValue.image.height, image.height);

    odysea::core::ThumbnailKey stale = key;
    ++stale.modified_seconds;
    const std::optional<odysea::core::StoredThumbnail> staleRecord = store.load(stale, error);
    QVERIFY(!error);
    QVERIFY(staleRecord.has_value());
    QVERIFY(!odysea::core::thumbnail_matches(staleRecord.value_or({}), stale));

    odysea::core::ThumbnailKey resized = key;
    ++resized.size;
    const std::optional<odysea::core::StoredThumbnail> resizedRecord = store.load(resized, error);
    QVERIFY(!error);
    QVERIFY(resizedRecord.has_value());
    QVERIFY(!odysea::core::thumbnail_matches(resizedRecord.value_or({}), resized));
}

QTEST_GUILESS_MAIN(ThumbnailBackendTest)

#include "tst_thumbnail_backend.moc"
