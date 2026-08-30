#include "presentation_capabilities.hpp"

#include <QObject>
#include <QQuickWindow>
#include <QSGRendererInterface>
#include <QSignalSpy>
#include <QTest>

namespace {

class ShellPresentationState final : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool alphaBufferAvailable READ alphaBufferAvailable WRITE setAlphaBufferAvailable)
    Q_PROPERTY(bool rendererSupportsWindowTransparency READ rendererSupportsWindowTransparency WRITE
                   setRendererSupportsWindowTransparency)

  public:
    [[nodiscard]] bool alphaBufferAvailable() const { return alphaBufferAvailable_; }
    void setAlphaBufferAvailable(bool available) { alphaBufferAvailable_ = available; }

    [[nodiscard]] bool rendererSupportsWindowTransparency() const {
        return rendererSupportsWindowTransparency_;
    }
    void setRendererSupportsWindowTransparency(bool supports) {
        rendererSupportsWindowTransparency_ = supports;
    }

  private:
    bool alphaBufferAvailable_{false};
    bool rendererSupportsWindowTransparency_{false};
};

} // namespace

class PresentationCapabilitiesTest final : public QObject {
    Q_OBJECT

  private slots:
    void rendererCapabilityBoundary();
    void softwareSceneGraphPublishesOpaqueFallback();
};

void PresentationCapabilitiesTest::rendererCapabilityBoundary() {
    QVERIFY(!odysea::app::rendererSupportsWindowTransparency(QSGRendererInterface::Unknown));
    QVERIFY(!odysea::app::rendererSupportsWindowTransparency(QSGRendererInterface::Software));
    QVERIFY(!odysea::app::rendererSupportsWindowTransparency(QSGRendererInterface::Null));
    QVERIFY(odysea::app::rendererSupportsWindowTransparency(QSGRendererInterface::OpenGL));
}

void PresentationCapabilitiesTest::softwareSceneGraphPublishesOpaqueFallback() {
    // The CTest entry forces QT_QUICK_BACKEND=software. Checking the renderer
    // after scene-graph initialization proves that selector reached Qt; a
    // misspelled RHI variable would leave the default renderer in place.
    QQuickWindow::setDefaultAlphaBuffer(true);
    QQuickWindow window;
    ShellPresentationState shell;
    odysea::app::publishWindowPresentationCapabilities(window, shell);

    QSignalSpy initialized(&window, &QQuickWindow::sceneGraphInitialized);
    QVERIFY(initialized.isValid());
    window.resize(160, 100);
    window.show();
    QTRY_VERIFY_WITH_TIMEOUT(initialized.count() > 0, 5000);

    QCOMPARE(window.rendererInterface()->graphicsApi(), QSGRendererInterface::Software);
    QCOMPARE(shell.alphaBufferAvailable(), window.format().alphaBufferSize() > 0);
    QVERIFY(!shell.rendererSupportsWindowTransparency());
    window.hide();
}

QTEST_MAIN(PresentationCapabilitiesTest)

#include "tst_presentation_capabilities.moc"
