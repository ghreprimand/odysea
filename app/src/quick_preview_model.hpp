// Cancellable, off-thread content loading for the shell's quick preview.
#pragma once

#include <QImage>
#include <QObject>
#include <qqmlintegration.h>
#include <QString>
#include <QUrl>

#include <cstdint>
#include <memory>
#include <vector>

class QuickPreviewModel : public QObject {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(State state READ state NOTIFY contentChanged)
    Q_PROPERTY(ContentKind contentKind READ contentKind NOTIFY contentChanged)
    Q_PROPERTY(QString sourcePath READ sourcePath NOTIFY contentChanged)
    Q_PROPERTY(QString displayName READ displayName NOTIFY contentChanged)
    Q_PROPERTY(QString text READ text NOTIFY contentChanged)
    Q_PROPERTY(QImage image READ image NOTIFY contentChanged)
    Q_PROPERTY(QString message READ message NOTIFY contentChanged)
    Q_PROPERTY(bool truncated READ truncated NOTIFY contentChanged)
    Q_PROPERTY(bool loading READ loading NOTIFY contentChanged)
    Q_PROPERTY(int activeLoadCount READ activeLoadCount NOTIFY activeLoadCountChanged)

  public:
    enum class State : std::uint8_t { Idle, Loading, Ready, Unsupported, Error };
    Q_ENUM(State)

    enum class ContentKind : std::uint8_t { NoContent, RasterImage, PlainText, MarkdownDocument };
    Q_ENUM(ContentKind)

    Q_DISABLE_COPY_MOVE(QuickPreviewModel)

    explicit QuickPreviewModel(QObject* parent = nullptr);
    ~QuickPreviewModel() override;

    [[nodiscard]] State state() const noexcept;
    [[nodiscard]] ContentKind contentKind() const noexcept;
    [[nodiscard]] QString sourcePath() const;
    [[nodiscard]] QString displayName() const;
    [[nodiscard]] QString text() const;
    [[nodiscard]] QImage image() const;
    [[nodiscard]] QString message() const;
    [[nodiscard]] bool truncated() const noexcept;
    [[nodiscard]] bool loading() const noexcept;
    [[nodiscard]] int activeLoadCount() const noexcept;

    Q_INVOKABLE void open(const QUrl& sourceUrl);
    Q_INVOKABLE void cancel();

  signals:
    void contentChanged();
    void activeLoadCountChanged();
    /// Emitted only after a worker observes a cancellation request. This is
    /// distinct from hiding stale output by generation and makes cooperative
    /// cancellation observable without exposing worker implementation details.
    void loadCancelled();

  private:
    struct LoadJob;

    void resetContent(State state);
    void finish(const std::shared_ptr<LoadJob>& job);
    void cancelJobs();

    State state_ = State::Idle;
    ContentKind contentKind_ = ContentKind::NoContent;
    QString sourcePath_;
    QString displayName_;
    QString text_;
    QImage image_;
    QString message_;
    bool truncated_ = false;
    std::uint64_t generation_ = 0;
    std::vector<std::shared_ptr<LoadJob>> jobs_;
};
