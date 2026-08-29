import QtQuick

// The Storage Tube presentation pipeline over the shell content.
//
// Chain: content -> thresholded bright pass (well-mask exempt) -> a tight
// core blur and a wide halo blur (separable Gaussians) -> one composite
// that adds the emission and then rides scanlines, vignette, and dither on
// the added light. When every stage is at identity the chain disengages and
// the content renders on the plain path, byte-identical to having no
// pipeline at all.
//
// Every rendering parameter derives from the theme's effective* properties
// — the stored preference after accessibility overrides — never from the
// stored values the controls display. The mapping from the two bloom gains
// to threshold and blur width reproduces the preset tables exactly at the
// Balanced and Strong presets and interpolates linearly elsewhere.
//
// Capability fallback is silent: on the software scene graph, and on any
// backend where a shader stage fails to build, the chain never engages and
// the content stays on the plain path. Color roles, typography, the deep
// field ground, and text lift are palette- and geometry-side, so they
// survive every fallback.
Item {
    id: layer

    required property Item content
    required property Item wellMask
    required property var theme

    readonly property bool softwareBackend: GraphicsInfo.api === GraphicsInfo.Software
    // Latched: one failed stage disables the pipeline for the session
    // instead of flickering against a broken driver.
    property bool shaderFailed: false
    readonly property bool pipelineAvailable: !layer.softwareBackend && !layer.shaderFailed

    // Rendering parameters. Effective values only — binding any of these to
    // the stored preference properties is a regression the presentation
    // tests reject.
    readonly property real coreIntensity: layer.theme.effectiveBloomCore
    readonly property real wideIntensity: layer.theme.effectiveBloomWide
    readonly property real scanIntensity: layer.theme.effectiveScanline
    readonly property real vignetteIntensity: layer.theme.effectiveVignette

    // Emitter drive: the stronger of the two bloom demands. 0.75 weights
    // the wide gain so both accepted presets resolve exactly.
    readonly property real emitDrive: Math.max(layer.coreIntensity, 0.75 * layer.wideIntensity)
    readonly property real threshold: Math.min(1.0, Math.max(0.30, 0.85 - layer.emitDrive * (2.0 / 3.0)))

    readonly property real dpr: Screen.devicePixelRatio || 1
    readonly property real deviceScale: layer.dpr * layer.theme.uiScale
    // Core blur width in logical px, fit through the preset table
    // (Balanced 1.5 at gain 0.45, Strong 1.8 at gain 0.60).
    readonly property real sigmaCore: (0.6 + 2.0 * layer.coreIntensity) * layer.deviceScale
    // Wide halo reach: 16 logical px of true Gaussian sigma.
    readonly property real sigmaWide: 16.0 * layer.deviceScale
    readonly property real scanPeriodPx: 7.0

    /// Shared motion token: how long a moving highlight persists. Reduced
    /// motion zeroes effectivePersistence, which makes every consumer
    /// instant.
    readonly property int motionDurationMs: Math.round(160 * layer.theme.effectivePersistence)

    readonly property bool emissionActive: layer.pipelineAvailable && (layer.coreIntensity > 0.001 || layer.wideIntensity > 0.001)
    readonly property bool active: layer.pipelineAvailable && (layer.emissionActive || layer.scanIntensity > 0.001 || layer.vignetteIntensity > 0.001)
    // Context markers do not own a second halo path. They emit only through
    // this layer's bright pass and two existing blur chains. High contrast
    // keeps the crisp marker but declines the extra emission treatment.
    readonly property bool contextGlowAvailable: layer.emissionActive && !layer.theme.highContrast

    ShaderEffectSource {
        id: contentSrc

        sourceItem: layer.active ? layer.content : null
        hideSource: layer.active
        visible: false
        live: true
    }

    ShaderEffectSource {
        id: wellMaskSrc

        sourceItem: layer.active ? layer.wellMask : null
        hideSource: true
        visible: false
        live: true
        // A mask edge carries geometric coverage, not a nearest texel label.
        // At a device-pixel boundary a nearest read can select the transparent
        // texel beside a fully covered well pixel, exposing that pixel to
        // emission. The shaders classify the resulting coverage at 0.5, so
        // linear filtering keeps the whole covered edge protected without
        // expanding the mask into the one-pixel outside ring.
        smooth: true
    }

    // Kept observable for the device-pixel test: changing the mask back to a
    // nearest texture silently reopens the edge-leak class on a surface whose
    // source and destination texel grids land at different phases.
    readonly property bool wellMaskUsesLinearSampling: wellMaskSrc.smooth

    // The bright pass is present in the scene, hidden through hideSource on
    // its ShaderEffectSource, so the blur passes can take it as a direct
    // item source.
    EffectBrightPass {
        id: brightPass

        objectName: "presentationBrightPass"
        anchors.fill: parent
        visible: layer.emissionActive
        src: contentSrc
        mask: wellMaskSrc
        threshold: layer.threshold
        onStatusChanged: {
            if (status === ShaderEffect.Error) {
                layer.shaderFailed = true;
            }
        }
    }

    ShaderEffectSource {
        id: brightHide

        sourceItem: brightPass
        hideSource: true
        visible: false
        live: true
    }

    EffectBlurPass {
        id: coreH

        anchors.fill: parent
        visible: layer.emissionActive && layer.coreIntensity > 0.001
        src: brightHide
        srcSize: Qt.size(layer.width * layer.dpr, layer.height * layer.dpr)
        axis: Qt.vector2d(1, 0)
        sigma: layer.sigmaCore
        onStatusChanged: {
            if (status === ShaderEffect.Error) {
                layer.shaderFailed = true;
            }
        }
    }

    ShaderEffectSource {
        id: coreHSrc

        sourceItem: coreH
        hideSource: true
        visible: false
        live: true
    }

    EffectBlurPass {
        id: coreV

        objectName: "presentationCoreV"
        anchors.fill: parent
        visible: layer.emissionActive && layer.coreIntensity > 0.001
        src: coreHSrc
        srcSize: Qt.size(layer.width * layer.dpr, layer.height * layer.dpr)
        axis: Qt.vector2d(0, 1)
        sigma: layer.sigmaCore
        onStatusChanged: {
            if (status === ShaderEffect.Error) {
                layer.shaderFailed = true;
            }
        }
    }

    ShaderEffectSource {
        id: coreSrcTex

        sourceItem: coreV
        hideSource: true
        visible: false
        live: true
    }

    // Wide halo: x2 quantization-headroom encode per pass (x4 through the
    // chain; the composite decodes by 0.25). Wide-stage magnitudes peak
    // well under half scale pre-gain, so the encode cannot clip.
    EffectBlurPass {
        id: wideH

        anchors.fill: parent
        visible: layer.emissionActive && layer.wideIntensity > 0.001
        src: brightHide
        srcSize: Qt.size(layer.width * layer.dpr, layer.height * layer.dpr)
        axis: Qt.vector2d(1, 0)
        sigma: layer.sigmaWide
        gain: 2.0
        onStatusChanged: {
            if (status === ShaderEffect.Error) {
                layer.shaderFailed = true;
            }
        }
    }

    ShaderEffectSource {
        id: wideHSrc

        sourceItem: wideH
        hideSource: true
        visible: false
        live: true
    }

    EffectBlurPass {
        id: wideV

        objectName: "presentationWideV"
        anchors.fill: parent
        visible: layer.emissionActive && layer.wideIntensity > 0.001
        src: wideHSrc
        srcSize: Qt.size(layer.width * layer.dpr, layer.height * layer.dpr)
        axis: Qt.vector2d(0, 1)
        sigma: layer.sigmaWide
        gain: 2.0
        onStatusChanged: {
            if (status === ShaderEffect.Error) {
                layer.shaderFailed = true;
            }
        }
    }

    ShaderEffectSource {
        id: wideSrcTex

        sourceItem: wideV
        hideSource: true
        visible: false
        live: true
        smooth: true
    }

    EffectComposite {
        objectName: "presentationComposite"
        anchors.fill: parent
        visible: layer.active
        src: contentSrc
        mask: wellMaskSrc
        coreSrc: coreSrcTex
        wideSrc: wideSrcTex
        coreI: layer.coreIntensity
        wideI: layer.wideIntensity * 0.25
        scanI: layer.scanIntensity
        period: layer.scanPeriodPx
        vigI: layer.vignetteIntensity
        dpr: layer.dpr
        translucentGround: layer.theme.glassOpacity < 0.999
        onStatusChanged: {
            if (status === ShaderEffect.Error) {
                layer.shaderFailed = true;
            }
        }
    }
}
