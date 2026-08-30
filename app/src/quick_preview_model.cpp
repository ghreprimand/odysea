#include "quick_preview_model.hpp"

#include <QFile>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QImageReader>
#include <QMimeDatabase>
#include <QMimeType>
#include <QtConcurrentRun>

#include <algorithm>
#include <array>
#include <atomic>
#include <ranges>
#include <utility>

namespace {

constexpr qsizetype kTextLimit = qsizetype{1024} * qsizetype{1024};
constexpr qsizetype kTextChunk = qsizetype{64} * qsizetype{1024};
constexpr int kPreviewWidth = 1600;
constexpr int kPreviewHeight = 1200;

struct LoadResult {
    QuickPreviewModel::State state = QuickPreviewModel::State::Error;
    QuickPreviewModel::ContentKind kind = QuickPreviewModel::ContentKind::NoContent;
    QString text;
    QImage image;
    QString message;
    bool truncated = false;
    bool cancelled = false;
};

[[nodiscard]] LoadResult cancelledResult() {
    LoadResult result;
    result.cancelled = true;
    return result;
}

[[nodiscard]] bool cancellationRequested(const std::shared_ptr<std::atomic_bool>& cancelled) {
    return cancelled->load(std::memory_order_relaxed);
}

[[nodiscard]] bool isMarkdown(const QFileInfo& info, const QMimeType& mime) {
    const QString suffix = info.suffix().toLower();
    return suffix == QStringLiteral("md") || suffix == QStringLiteral("markdown") ||
           mime.name() == QStringLiteral("text/markdown");
}

[[nodiscard]] bool isUnsupportedDocument(const QFileInfo& info, const QMimeType& mime) {
    static constexpr std::array<const char*, 8> suffixes = {"pdf", "odt",  "ods", "odp",
                                                            "doc", "docx", "xls", "xlsx"};
    const QByteArray suffix = info.suffix().toLower().toLatin1();
    if (std::ranges::any_of(suffixes,
                            [&suffix](const char* candidate) { return suffix == candidate; })) {
        return true;
    }
    return mime.name() == QStringLiteral("application/pdf") ||
           mime.name().startsWith(QStringLiteral("application/vnd.oasis.opendocument")) ||
           mime.name().startsWith(QStringLiteral("application/vnd.openxmlformats"));
}

[[nodiscard]] LoadResult loadText(const QString& path, QuickPreviewModel::ContentKind kind,
                                  const std::shared_ptr<std::atomic_bool>& cancelled) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return {.state = QuickPreviewModel::State::Error,
                .kind = kind,
                .text = {},
                .image = {},
                .message = QObject::tr("The file could not be opened for preview."),
                .truncated = false,
                .cancelled = false};
    }

    QByteArray bytes;
    bytes.reserve(kTextLimit);
    while (bytes.size() < kTextLimit) {
        if (cancellationRequested(cancelled)) {
            return cancelledResult();
        }
        const qsizetype remaining = kTextLimit - bytes.size();
        const QByteArray chunk = file.read(std::min(kTextChunk, remaining));
        if (chunk.isEmpty()) {
            if (file.error() != QFileDevice::NoError) {
                return {.state = QuickPreviewModel::State::Error,
                        .kind = kind,
                        .text = {},
                        .image = {},
                        .message = QObject::tr("The file could not be read for preview."),
                        .truncated = false,
                        .cancelled = false};
            }
            break;
        }
        bytes.append(chunk);
    }

    if (cancellationRequested(cancelled)) {
        return cancelledResult();
    }
    const bool truncated = !file.atEnd();
    return {.state = QuickPreviewModel::State::Ready,
            .kind = kind,
            .text = QString::fromUtf8(bytes),
            .image = {},
            .message = truncated ? QObject::tr("Preview limited to the first 1 MiB.") : QString{},
            .truncated = truncated,
            .cancelled = false};
}

[[nodiscard]] LoadResult loadImage(const QString& path,
                                   const std::shared_ptr<std::atomic_bool>& cancelled) {
    if (cancellationRequested(cancelled)) {
        return cancelledResult();
    }

    QImageReader reader(path);
    reader.setAutoTransform(true);
    const QSize originalSize = reader.size();
    if (originalSize.isValid()) {
        const QSize previewSize =
            originalSize.scaled(kPreviewWidth, kPreviewHeight, Qt::KeepAspectRatio);
        if (previewSize.width() < originalSize.width() ||
            previewSize.height() < originalSize.height()) {
            reader.setScaledSize(previewSize);
        }
    }

    if (cancellationRequested(cancelled)) {
        return cancelledResult();
    }
    QImage image = reader.read();
    if (cancellationRequested(cancelled)) {
        return cancelledResult();
    }
    if (image.isNull()) {
        return {.state = QuickPreviewModel::State::Error,
                .kind = QuickPreviewModel::ContentKind::RasterImage,
                .text = {},
                .image = {},
                .message = QObject::tr("The image could not be decoded for preview."),
                .truncated = false,
                .cancelled = false};
    }
    return {.state = QuickPreviewModel::State::Ready,
            .kind = QuickPreviewModel::ContentKind::RasterImage,
            .text = {},
            .image = std::move(image),
            .message = {},
            .truncated = false,
            .cancelled = false};
}

[[nodiscard]] LoadResult load(const QString& path,
                              const std::shared_ptr<std::atomic_bool>& cancelled) {
    if (cancellationRequested(cancelled)) {
        return cancelledResult();
    }

    const QFileInfo info(path);
    if (!info.exists() || !info.isFile()) {
        return {.state = QuickPreviewModel::State::Error,
                .kind = QuickPreviewModel::ContentKind::NoContent,
                .text = {},
                .image = {},
                .message = QObject::tr("The selected entry is not a readable file."),
                .truncated = false,
                .cancelled = false};
    }

    const QMimeType mime = QMimeDatabase{}.mimeTypeForFile(path, QMimeDatabase::MatchContent);
    if (isMarkdown(info, mime)) {
        return loadText(path, QuickPreviewModel::ContentKind::MarkdownDocument, cancelled);
    }
    if (mime.name().startsWith(QStringLiteral("image/"))) {
        return loadImage(path, cancelled);
    }
    if (mime.inherits(QStringLiteral("text/plain"))) {
        return loadText(path, QuickPreviewModel::ContentKind::PlainText, cancelled);
    }
    if (isUnsupportedDocument(info, mime)) {
        return {.state = QuickPreviewModel::State::Unsupported,
                .kind = QuickPreviewModel::ContentKind::NoContent,
                .text = {},
                .image = {},
                .message = QObject::tr(
                    "This build has no renderer for this document format. Markdown documents "
                    "are supported without an additional rendering dependency."),
                .truncated = false,
                .cancelled = false};
    }
    return {.state = QuickPreviewModel::State::Unsupported,
            .kind = QuickPreviewModel::ContentKind::NoContent,
            .text = {},
            .image = {},
            .message = QObject::tr("Quick preview supports raster images, plain text, and "
                                   "Markdown documents."),
            .truncated = false,
            .cancelled = false};
}

} // namespace

struct QuickPreviewModel::LoadJob {
    std::uint64_t generation = 0;
    std::shared_ptr<std::atomic_bool> cancelled = std::make_shared<std::atomic_bool>(false);
    std::unique_ptr<QFutureWatcher<LoadResult>> watcher =
        std::make_unique<QFutureWatcher<LoadResult>>();
};

QuickPreviewModel::QuickPreviewModel(QObject* parent) : QObject(parent) {}

QuickPreviewModel::~QuickPreviewModel() {
    cancelJobs();
    for (const auto& job : jobs_) {
        job->watcher->waitForFinished();
    }
}

QuickPreviewModel::State QuickPreviewModel::state() const noexcept {
    return state_;
}

QuickPreviewModel::ContentKind QuickPreviewModel::contentKind() const noexcept {
    return contentKind_;
}

QString QuickPreviewModel::sourcePath() const {
    return sourcePath_;
}

QString QuickPreviewModel::displayName() const {
    return displayName_;
}

QString QuickPreviewModel::text() const {
    return text_;
}

QImage QuickPreviewModel::image() const {
    return image_;
}

QString QuickPreviewModel::message() const {
    return message_;
}

bool QuickPreviewModel::truncated() const noexcept {
    return truncated_;
}

bool QuickPreviewModel::loading() const noexcept {
    return state_ == State::Loading;
}

int QuickPreviewModel::activeLoadCount() const noexcept {
    return static_cast<int>(jobs_.size());
}

void QuickPreviewModel::open(const QUrl& sourceUrl) {
    cancelJobs();
    ++generation_;
    const QString sourcePath = sourceUrl.isLocalFile() ? sourceUrl.toLocalFile() : QString{};
    sourcePath_ = sourcePath;
    displayName_ = QFileInfo(sourcePath).fileName();
    resetContent(State::Loading);

    auto job = std::make_shared<LoadJob>();
    job->generation = generation_;
    const std::weak_ptr<LoadJob> weakJob = job;
    connect(job->watcher.get(), &QFutureWatcher<LoadResult>::finished, this, [this, weakJob] {
        if (const std::shared_ptr<LoadJob> finishedJob = weakJob.lock()) {
            finish(finishedJob);
        }
    });
    jobs_.push_back(job);
    emit activeLoadCountChanged();
    job->watcher->setFuture(QtConcurrent::run(
        [sourcePath, cancelled = job->cancelled] { return load(sourcePath, cancelled); }));
}

void QuickPreviewModel::cancel() {
    cancelJobs();
    ++generation_;
    sourcePath_.clear();
    displayName_.clear();
    resetContent(State::Idle);
}

void QuickPreviewModel::resetContent(State state) {
    state_ = state;
    contentKind_ = ContentKind::NoContent;
    text_.clear();
    image_ = {};
    message_.clear();
    truncated_ = false;
    emit contentChanged();
}

void QuickPreviewModel::finish(const std::shared_ptr<LoadJob>& job) {
    const LoadResult result = job->watcher->result();
    const std::uint64_t jobGeneration = job->generation;
    const auto iterator = std::ranges::find(jobs_, job);
    if (iterator != jobs_.end()) {
        jobs_.erase(iterator);
        emit activeLoadCountChanged();
    }

    if (result.cancelled) {
        emit loadCancelled();
        return;
    }
    if (jobGeneration != generation_) {
        return;
    }

    state_ = result.state;
    contentKind_ = result.kind;
    text_ = result.text;
    image_ = result.image;
    message_ = result.message;
    truncated_ = result.truncated;
    emit contentChanged();
}

void QuickPreviewModel::cancelJobs() {
    for (const auto& job : jobs_) {
        job->cancelled->store(true, std::memory_order_relaxed);
    }
}
