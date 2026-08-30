#include "presentation_capabilities.hpp"

#include <QObject>
#include <QQuickWindow>
#include <QSGRendererInterface>
#include <QSignalSpy>
#include <QTest>

#include <array>

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
    struct RendererCase {
        QSGRendererInterface::GraphicsApi api;
        bool preservesAlpha;
    };
    constexpr std::array<RendererCase, 9> cases{
        RendererCase{.api = QSGRendererInterface::Unknown, .preservesAlpha = false},
        RendererCase{.api = QSGRendererInterface::Software, .preservesAlpha = false},
        RendererCase{.api = QSGRendererInterface::OpenVG, .preservesAlpha = true},
        RendererCase{.api = QSGRendererInterface::OpenGL, .preservesAlpha = true},
        RendererCase{.api = QSGRendererInterface::Direct3D11, .preservesAlpha = true},
        RendererCase{.api = QSGRendererInterface::Vulkan, .preservesAlpha = true},
        RendererCase{.api = QSGRendererInterface::Metal, .preservesAlpha = true},
        RendererCase{.api = QSGRendererInterface::Null, .preservesAlpha = false},
        RendererCase{.api = QSGRendererInterface::Direct3D12, .preservesAlpha = true},
    };

    for (const RendererCase& renderer : cases) {
        QCOMPARE(odysea::app::rendererPreservesWindowAlpha(renderer.api), renderer.preservesAlpha);
    }
}

void PresentationCapabilitiesTest::softwareSceneGraphPublishesOpaqueFallback() {
    // The CTest entry forces QT_QUICK_BACKEND=software. Checking the renderer
    // after scene-graph initialization proves that selector reached Qt; a
    // misspelled RHI variable would leave the default renderer in place.
    QQuickWindow::setDefaultAlphaBuffer(true);
    QQuickWindow window;
    window.setColor(Qt::black);
    ShellPresentationState shell;
    odysea::app::publishWindowPresentationCapabilities(window, shell);

    QSignalSpy initialized(&window, &QQuickWindow::sceneGraphInitialized);
    QVERIFY(initialized.isValid());
    window.resize(160, 100);
    window.show();
    QTRY_VERIFY_WITH_TIMEOUT(initialized.count() > 0, 5000);

    QCOMPARE(window.rendererInterface()->graphicsApi(), QSGRendererInterface::Software);
    QVERIFY(window.requestedFormat().alphaBufferSize() > 0);
    QCOMPARE(shell.alphaBufferAvailable(), window.format().alphaBufferSize() > 0);
    QVERIFY(!shell.rendererSupportsWindowTransparency());
    window.hide();
}

QTEST_MAIN(PresentationCapabilitiesTest)

#include "tst_presentation_capabilities.moc"
