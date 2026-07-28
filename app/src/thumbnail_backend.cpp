#include "thumbnail_backend.hpp"

#include <QByteArray>
#include <QImageReader>
#include <QImageWriter>
#include <QSaveFile>
#include <QString>

#include <algorithm>
#include <cstring>
#include <limits>

namespace {

constexpr std::size_t bytesPerPixel = 4;

[[nodiscard]] std::error_code makeError(std::errc code) {
    return std::make_error_code(code);
}

[[nodiscard]] bool dimensionsFit(const QSize& size, const ThumbnailDecodeLimits& limits,
                                 std::error_code& error) {
    if (!size.isValid() || size.width() <= 0 || size.height() <= 0) {
        error = makeError(std::errc::invalid_argument);
        return false;
    }

    const auto width = static_cast<std::uint64_t>(size.width());
    const auto height = static_cast<std::uint64_t>(size.height());
    if (width > limits.max_dimension || height > limits.max_dimension) {
        error = makeError(std::errc::value_too_large);
        return false;
    }
    if (width > std::numeric_limits<std::uint64_t>::max() / height) {
        error = makeError(std::errc::value_too_large);
        return false;
    }
    const std::uint64_t pixels = width * height;
    if (pixels > static_cast<std::uint64_t>(limits.max_decoded_bytes / bytesPerPixel)) {
        error = makeError(std::errc::value_too_large);
        return false;
    }
    return true;
}

[[nodiscard]] bool formatIsAllowed(const QByteArray& format) {
    const QByteArray normalized = format.toLower();
    return normalized == "png" || normalized == "jpeg" || normalized == "jpg" ||
           normalized == "webp";
}

[[nodiscard]] QString pathString(const std::filesystem::path& path) {
    return QString::fromStdString(path.string());
}

[[nodiscard]] int allocationLimitMegabytes(const ThumbnailDecodeLimits& limits) noexcept {
    constexpr std::size_t bytesPerMegabyte = 1024UL * 1024UL;
    const std::size_t rounded = limits.max_decoded_bytes / bytesPerMegabyte +
                                (limits.max_decoded_bytes % bytesPerMegabyte == 0 ? 0UL : 1UL);
    return static_cast<int>(std::clamp(rounded, std::size_t{1},
                                       static_cast<std::size_t>(std::numeric_limits<int>::max())));
}

} // namespace

QImage thumbnailImageToQImage(const odysea::core::ThumbnailImage& source, std::error_code& error) {
    error.clear();
    if (source.width == 0 || source.height == 0 ||
        source.width > static_cast<std::uint32_t>(std::numeric_limits<int>::max()) ||
        source.height > static_cast<std::uint32_t>(std::numeric_limits<int>::max())) {
        error = makeError(std::errc::invalid_argument);
        return {};
    }

    const std::size_t width = source.width;
    const std::size_t height = source.height;
    if (width > std::numeric_limits<std::size_t>::max() / height ||
        width * height > std::numeric_limits<std::size_t>::max() / bytesPerPixel ||
        source.pixels.size() != width * height * bytesPerPixel) {
        error = makeError(std::errc::invalid_argument);
        return {};
    }

    QImage image(static_cast<int>(source.width), static_cast<int>(source.height),
                 QImage::Format_RGBA8888);
    if (image.isNull()) {
        error = makeError(std::errc::not_enough_memory);
        return {};
    }
    const std::size_t rowBytes = width * bytesPerPixel;
    for (std::size_t row = 0; row < height; ++row) {
        std::memcpy(image.scanLine(static_cast<int>(row)), source.pixels.data() + row * rowBytes,
                    rowBytes);
    }
    return image;
}

odysea::core::ThumbnailImage qImageToThumbnailImage(const QImage& source, std::error_code& error) {
    error.clear();
    if (source.isNull() || source.width() <= 0 || source.height() <= 0) {
        error = makeError(std::errc::invalid_argument);
        return {};
    }

    const QImage converted = source.convertToFormat(QImage::Format_RGBA8888);
    if (converted.isNull()) {
        error = makeError(std::errc::not_enough_memory);
        return {};
    }
    const auto width = static_cast<std::size_t>(converted.width());
    const auto height = static_cast<std::size_t>(converted.height());
    if (width > std::numeric_limits<std::size_t>::max() / height ||
        width * height > std::numeric_limits<std::size_t>::max() / bytesPerPixel) {
        error = makeError(std::errc::value_too_large);
        return {};
    }

    odysea::core::ThumbnailImage image;
    image.width = static_cast<std::uint32_t>(converted.width());
    image.height = static_cast<std::uint32_t>(converted.height());
    const std::size_t rowBytes = width * bytesPerPixel;
    image.pixels.resize(rowBytes * height);
    for (std::size_t row = 0; row < height; ++row) {
        std::memcpy(image.pixels.data() + row * rowBytes,
                    converted.constScanLine(static_cast<int>(row)), rowBytes);
    }
    return image;
}

QtThumbnailProducer::QtThumbnailProducer(ThumbnailDecodeLimits limits) : limits_(limits) {}

odysea::core::ThumbnailImage QtThumbnailProducer::produce(const std::filesystem::path& source,
                                                          odysea::core::ThumbnailSize size,
                                                          std::error_code& error) {
    error.clear();
    const std::filesystem::file_status status = std::filesystem::status(source, error);
    if (error) {
        return {};
    }
    if (!std::filesystem::is_regular_file(status)) {
        error = makeError(std::errc::invalid_argument);
        return {};
    }

    QImageReader::setAllocationLimit(allocationLimitMegabytes(limits_));
    QImageReader reader(pathString(source));
    reader.setDecideFormatFromContent(true);
    reader.setAutoTransform(true);

    const QByteArray format = reader.format();
    if (!formatIsAllowed(format)) {
        error = makeError(std::errc::operation_not_supported);
        return {};
    }

    const QSize sourceSize = reader.size();
    if (!dimensionsFit(sourceSize, limits_, error)) {
        return {};
    }

    const int edge = static_cast<int>(odysea::core::thumbnail_edge_pixels(size));
    if (sourceSize.width() > edge || sourceSize.height() > edge) {
        reader.setScaledSize(sourceSize.scaled(edge, edge, Qt::KeepAspectRatio));
    }

    const QImage decoded = reader.read();
    if (decoded.isNull()) {
        error = makeError(std::errc::io_error);
        return {};
    }
    return qImageToThumbnailImage(decoded, error);
}

FreedesktopThumbnailStore::FreedesktopThumbnailStore(ThumbnailDecodeLimits limits)
    : limits_(limits) {}

std::optional<odysea::core::StoredThumbnail>
FreedesktopThumbnailStore::load(const odysea::core::ThumbnailKey& key, std::error_code& error) {
    error.clear();
    const std::filesystem::path path = odysea::core::thumbnail_path(key, error);
    if (error) {
        return std::nullopt;
    }

    std::error_code existsError;
    const bool exists = std::filesystem::exists(path, existsError);
    if (existsError) {
        error = existsError;
        return std::nullopt;
    }
    if (!exists) {
        return std::nullopt;
    }

    QImageReader::setAllocationLimit(allocationLimitMegabytes(limits_));
    QImageReader reader(pathString(path), QByteArrayLiteral("png"));
    const QSize storedSize = reader.size();
    if (!dimensionsFit(storedSize, limits_, error)) {
        return std::nullopt;
    }
    const QImage decoded = reader.read();
    if (decoded.isNull()) {
        error = makeError(std::errc::io_error);
        return std::nullopt;
    }
    const QString uriText = decoded.text(QStringLiteral("Thumb::URI"));
    const QString modifiedText = decoded.text(QStringLiteral("Thumb::MTime"));
    const QString sourceSizeText = decoded.text(QStringLiteral("Thumb::Size"));

    odysea::core::StoredThumbnail stored;
    stored.image = qImageToThumbnailImage(decoded, error);
    if (error) {
        return std::nullopt;
    }
    stored.uri = uriText.toStdString();

    bool modifiedValid = false;
    stored.modified_seconds = modifiedText.toLongLong(&modifiedValid);
    if (!modifiedValid) {
        stored.modified_seconds = std::numeric_limits<std::int64_t>::min();
    }

    bool sizeValid = false;
    stored.size = sourceSizeText.toULongLong(&sizeValid);
    stored.size_recorded = sizeValid;
    return stored;
}

void FreedesktopThumbnailStore::save(const odysea::core::ThumbnailKey& key,
                                     const odysea::core::ThumbnailImage& image,
                                     std::error_code& error) {
    error.clear();
    const std::filesystem::path path = odysea::core::thumbnail_path(key, error);
    if (error) {
        return;
    }

    QImage encoded = thumbnailImageToQImage(image, error);
    if (error) {
        return;
    }
    if (!dimensionsFit(encoded.size(), limits_, error)) {
        return;
    }

    std::filesystem::create_directories(path.parent_path(), error);
    if (error) {
        return;
    }

    QSaveFile output(pathString(path));
    if (!output.open(QIODevice::WriteOnly)) {
        error = makeError(std::errc::io_error);
        return;
    }

    encoded.setText(QStringLiteral("Thumb::URI"), QString::fromStdString(key.uri));
    encoded.setText(QStringLiteral("Thumb::MTime"), QString::number(key.modified_seconds));
    encoded.setText(QStringLiteral("Thumb::Size"),
                    QString::number(static_cast<qulonglong>(key.size)));
    QImageWriter writer(&output, QByteArrayLiteral("png"));
    if (!writer.write(encoded) || !output.commit()) {
        output.cancelWriting();
        error = makeError(std::errc::io_error);
    }
}
