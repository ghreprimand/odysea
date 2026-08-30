// A Qt Quick image surface that accepts the off-thread loader's QImage.
#pragma once

#include <QImage>
#include <qqmlintegration.h>
#include <QQuickPaintedItem>

class PreviewImageItem : public QQuickPaintedItem {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QImage source READ source WRITE setSource NOTIFY sourceChanged)

  public:
    explicit PreviewImageItem(QQuickItem* parent = nullptr);

    [[nodiscard]] QImage source() const;
    void setSource(const QImage& source);
    void paint(QPainter* painter) override;

  signals:
    void sourceChanged();

  private:
    QImage source_;
};
