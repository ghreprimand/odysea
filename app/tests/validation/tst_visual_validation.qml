// Visual-foundation acceptance over the real shell scene.
//
// Exercises the accepted visual system where it can regress: a density-aware
// sweep across the measured compact breakpoint keeps every chrome control
// reachable and no text clipped, focus indicators stay unambiguous with every
// effect disabled, reduced motion suppresses the motion-driven effects without
// disturbing layout, the shell stays fully usable with the effect layer off,
// and a directory large enough to exercise virtualization neither defeats it
// nor scales the effect layer's cost with entry count.
//
// The suite runs at 1x and, through a second software-path entry that sets
// QT_SCALE_FACTOR=2, with doubled logical scaling. Every assertion here is
// written in logical coordinates, so that second entry is not evidence of a
// genuine 2x framebuffer; device-resolution coverage belongs exclusively to
// the declared real-compositor entry.
pragma ComponentBehavior: Bound
import QtQuick
import QtTest
import OdySea
import "../support" as Support

Support.ShellTestCase {
    id: testCase

    name: "VisualValidation"

    readonly property var chromeControls: ["applicationIdentityMark", "backButton", "forwardButton", "upButton", "refreshButton", "undoButton", "paneToggleButton", "listViewButton", "gridViewButton", "columnsViewButton", "treeSearchButton", "paletteButton", "appearanceButton", "filterField", "sortModeBox", "hiddenToggle", "selectAllButton", "copyButton", "moveButton", "renameButton", "trashButton", "newTabButton", "statusMessageText", "pathNavigator"]
    // Large enough to put every viewport-derived bound at least tenfold below
    // the fixture size. This is a reproducible work-volume contract, not a
    // timing target: task scheduling cannot turn a larger directory into a
    // pass by running it on a quieter machine.
    readonly property int representativeLargeDirectoryEntries: 2000

    function theme() {
        return testCase.shellWindow.shellTheme;
    }

    function resizeShell(width, height) {
        testCase.shellWindow.width = width;
        testCase.shellWindow.height = height;
        // Give the platform a bounded moment to apply the requested size to
        // both the window and its content, without failing if it will not.
        // A Wayland client cannot set its own surface size — the compositor
        // owns geometry — so on that platform the requested size never takes;
        // some compositors resize the window but never propagate the size to
        // the content surface. Auditing a size-dependent layout against a size
        // the platform did not apply would measure a breakpoint that never
        // occurred, so this skips with the constraint named rather than
        // failing or, worse, passing silently over the wrong geometry. A
        // silent skip here would be the exact defect class the real-compositor
        // gate exists to close: a skip reading as a pass.
        const content = testCase.shellWindow.contentItem;
        for (let attempt = 0; attempt < 50 && (testCase.shellWindow.width !== width || testCase.shellWindow.height !== height || content.width !== width); ++attempt) {
            wait(20);
        }
        if (testCase.shellWindow.width !== width || testCase.shellWindow.height !== height || content.width !== width) {
            skip("this platform did not apply the requested size to the window and its content (window " + testCase.shellWindow.width + "x" + testCase.shellWindow.height + ", content width " + content.width + ", for a requested " + width + "x" + height + "); the size-dependent layout audit cannot run here");
        }
        waitForRendering(content);
    }

    // Keyboard-focus audits need the shell window to hold input activation:
    // an inactive window is delivered no key events and lands no active-focus
    // item, so a traversal or palette-key assertion would measure a window
    // that never received the keystrokes, not a product defect. A compositor
    // may withhold activation from a surface it is not presenting — a
    // background or non-visible window — which the client cannot override.
    // This requests activation and, if the signal never arrives, skips with
    // the constraint named rather than failing or, worse, passing over a
    // window that was never focused; the skip is written against the
    // activation signal itself, not a reproduction of the downstream keyboard
    // symptom. Every platform that activates the window — offscreen, xcb, and
    // any compositor that focuses the test surface — passes straight through
    // and runs the audit in full.
    function requireWindowActivation() {
        testCase.shellWindow.requestActivate();
        for (let attempt = 0; attempt < 50 && !testCase.shellWindow.active; ++attempt) {
            wait(20);
        }
        if (!testCase.shellWindow.active) {
            skip("this platform did not grant the shell window input activation; the keyboard-focus audit cannot run here");
        }
    }

    /// Runs after every test function: whatever a test changed — window
    /// size, profile, overrides — is restored even when it failed midway,
    /// so one failure never cascades into the tests behind it.
    function cleanup() {
        testCase.shellWindow.width = 1100;
        testCase.shellWindow.height = 720;
        testCase.shellWindow.alphaBufferAvailable = false;
        testCase.shellWindow.rendererSupportsWindowTransparency = false;
        theme().resetToDefaults();
    }

    /// Collects every visible Text item beneath `item`, skipping subtrees
    /// that are invisible or zero-sized.
    function collectVisibleTexts(item, into) {
        if (!item.visible || item.width <= 0 || item.height <= 0) {
            return;
        }
        if (item instanceof Text) {
            into.push(item);
        }
        for (let index = 0; index < item.children.length; ++index) {
            collectVisibleTexts(item.children[index], into);
        }
    }

    function auditChrome(label) {
        const content = testCase.shellWindow.contentItem;

        // Every chrome control stays reachable: visible, non-degenerate,
        // and entirely inside the window.
        for (let index = 0; index < testCase.chromeControls.length; ++index) {
            const name = testCase.chromeControls[index];
            const control = child(name);
            verify(control.visible, label + ": " + name + " must stay visible");
            verify(control.width > 0 && control.height > 0, label + ": " + name + " must keep a usable size");
            const topLeft = control.mapToItem(content, 0, 0);
            verify(topLeft.x >= -0.5 && topLeft.y >= -0.5, label + ": " + name + " must not leave the window");
            verify(topLeft.x + control.width <= content.width + 0.5, label + ": " + name + " must not overflow the right edge");
            verify(topLeft.y + control.height <= content.height + 0.5, label + ": " + name + " must not overflow the bottom edge");
        }

        // The chrome strips stack without overlap: each row of the shell
        // column begins at or below the end of the row above it.
        const column = child("pathNavigator").parent;
        const rows = [];
        for (let index = 0; index < column.children.length; ++index) {
            const row = column.children[index];
            if (row.visible && row.height > 0) {
                rows.push(row);
            }
        }
        rows.sort(function (a, b) {
            return a.y - b.y;
        });
        for (let index = 1; index < rows.length; ++index) {
            verify(rows[index].y >= rows[index - 1].y + rows[index - 1].height - 0.5, label + ": chrome rows must not overlap");
        }

        // A row can fit beside another strip while still painting its own
        // controls beyond the strip's edge. These direct-child bounds make
        // that condition observable instead of relying on a screenshot.
        const stripRows = [
            {
                "strip": child("navigationToolBar"),
                "rowName": "toolbarVisibleRow"
            },
            {
                "strip": child("pathNavigator"),
                "rowName": "calmPathRow"
            },
            {
                "strip": child("actionBar"),
                "rowName": "actionRow"
            }
        ];
        for (let stripIndex = 0; stripIndex < stripRows.length; ++stripIndex) {
            const entry = stripRows[stripIndex];
            const row = findChild(entry.strip, entry.rowName);
            verify(row !== null, label + ": missing " + entry.rowName);
            for (let childIndex = 0; childIndex < row.children.length; ++childIndex) {
                const rowChild = row.children[childIndex];
                if (!rowChild.visible || rowChild.width <= 0 || rowChild.height <= 0) {
                    continue;
                }
                const position = rowChild.mapToItem(content, 0, 0);
                const stripPosition = entry.strip.mapToItem(content, 0, 0);
                verify(position.x >= stripPosition.x - 0.5, label + ": " + entry.rowName + " child must not cross its strip's left edge");
                verify(position.y >= stripPosition.y - 0.5, label + ": " + entry.rowName + " child must not cross its strip's top edge");
                verify(position.x + rowChild.width <= stripPosition.x + entry.strip.width + 0.5, label + ": " + entry.rowName + " child must not cross its strip's right edge");
                verify(position.y + rowChild.height <= stripPosition.y + entry.strip.height + 0.5, label + ": " + entry.rowName + " child must not cross its strip's bottom edge");
            }
        }

        const toolbarRow = findChild(child("navigationToolBar"), "toolbarVisibleRow");
        const pathRow = findChild(child("pathNavigator"), "calmPathRow");
        const toolbarBottom = toolbarRow.mapToItem(content, 0, toolbarRow.height).y;
        const pathTop = pathRow.mapToItem(content, 0, 0).y;
        verify(pathTop - toolbarBottom >= 2 * child("navigationToolBar").chromeMargin - 0.5, label + ": toolbar and path content bands must retain their interior breathing room");

        // No visible label may overflow its bounds without eliding: painted
        // width beyond the item with elision disabled is clipped text.
        const texts = [];
        collectVisibleTexts(content, texts);
        verify(texts.length > 0, label + ": the audit must find rendered text");
        for (let index = 0; index < texts.length; ++index) {
            const text = texts[index];
            if (text.elide === Text.ElideNone) {
                verify(text.paintedWidth <= text.width + 0.5, label + ": text '" + text.text + "' must elide instead of clipping");
            }
        }
    }

    function test_densityAwareCompactBreakpointKeepsChromeIntact() {
        const densities = [ShellTheme.Compact, ShellTheme.Cozy, ShellTheme.Comfortable];
        const offsets = [-80, -60, -40, -20, -1, 0, 1, 20, 40, 60, 80];
        const actionBar = child("actionBar");
        let previousBreakpoint = 0;

        for (let densityIndex = 0; densityIndex < densities.length; ++densityIndex) {
            theme().density = densities[densityIndex];
            tryCompare(theme(), "density", densities[densityIndex]);
            waitForRendering(testCase.shellWindow.contentItem);

            const breakpoint = Math.ceil(actionBar.labeledWidthRequirement);
            verify(breakpoint > testCase.shellWindow.minimumWidth, "the sweep must cross a reachable compact breakpoint");
            verify(breakpoint > previousBreakpoint, "each wider density must move the measured breakpoint");
            previousBreakpoint = breakpoint;

            resizeShell(testCase.shellWindow.minimumWidth, testCase.shellWindow.minimumHeight);
            verify(actionBar.compact, "the minimum width must use compact chrome at density " + densityIndex);
            auditChrome("density " + densityIndex + ", minimum width");

            for (let offsetIndex = 0; offsetIndex < offsets.length; ++offsetIndex) {
                const requestedWidth = breakpoint + offsets[offsetIndex];
                resizeShell(requestedWidth, 720);
                compare(actionBar.compact, requestedWidth < breakpoint, "compact mode must follow the measured requirement at density " + densityIndex + ", width " + requestedWidth);
                auditChrome("density " + densityIndex + ", width " + requestedWidth);
            }

            resizeShell(1600, 900);
            verify(!actionBar.compact, "the wide layout must keep labels at density " + densityIndex);
            auditChrome("density " + densityIndex + ", wide");
        }
    }

    function test_uiScaleMovesMeasuredCompactBreakpoint() {
        const scales = [0.75, 2.0];
        const actionBar = child("actionBar");
        let previousBreakpoint = 0;

        theme().density = ShellTheme.Cozy;
        for (let scaleIndex = 0; scaleIndex < scales.length; ++scaleIndex) {
            theme().uiScale = scales[scaleIndex];
            tryCompare(theme(), "uiScale", scales[scaleIndex]);
            waitForRendering(testCase.shellWindow.contentItem);

            const breakpoint = Math.ceil(actionBar.labeledWidthRequirement);
            verify(breakpoint > previousBreakpoint, "a larger interface scale must move the measured breakpoint");
            previousBreakpoint = breakpoint;

            const requestedWidths = [breakpoint - 1, breakpoint, breakpoint + 1];
            for (let widthIndex = 0; widthIndex < requestedWidths.length; ++widthIndex) {
                const requestedWidth = requestedWidths[widthIndex];
                resizeShell(requestedWidth, 720);
                compare(actionBar.compact, requestedWidth < breakpoint, "compact mode must follow the measured requirement at scale " + scales[scaleIndex] + ", width " + requestedWidth);
                auditChrome("scale " + scales[scaleIndex] + ", width " + requestedWidth);
            }
        }
    }

    function test_toolbarAndPathUseMeasuredCompactBreakpoints() {
        const toolbar = child("navigationToolBar");
        const navigator = child("pathNavigator");
        const densities = [ShellTheme.Compact, ShellTheme.Cozy, ShellTheme.Comfortable];

        for (let densityIndex = 0; densityIndex < densities.length; ++densityIndex) {
            theme().density = densities[densityIndex];
            tryCompare(theme(), "density", densities[densityIndex]);
            waitForRendering(testCase.shellWindow.contentItem);

            const toolbarBreakpoint = Math.ceil(toolbar.labeledWidthRequirement);
            const navigatorBreakpoint = Math.ceil(navigator.labeledWidthRequirement);
            // Component coverage exercises each strip one pixel either side
            // of its own measured threshold. In the complete shell, retain
            // the product minimum width so a neighboring chrome row is never
            // asked to fit inside a window OdySea does not support.
            const supportedWidth = Math.max(testCase.shellWindow.minimumWidth, toolbarBreakpoint, navigatorBreakpoint);
            resizeShell(supportedWidth, 720);
            compare(toolbar.compact, false, "toolbar labels must fit at their measured requirement");
            compare(navigator.compact, false, "path labels must fit at their measured requirement");
            auditChrome("measured chrome requirements density " + densityIndex);
            resizeShell(testCase.shellWindow.minimumWidth, 720);
            auditChrome("minimum supported chrome density " + densityIndex);
        }
    }

    function test_windowTransparencyFollowsCapabilitiesProfilesAndAccessibility() {
        const root = testCase.shellWindow;
        const ground = child("windowGround");
        const toolbar = child("navigationToolBar");
        verify(ground !== null && toolbar !== null);

        // This models a requested alpha setting on a renderer that preserves
        // it. If the window-ground binding is removed, the first comparison
        // below fails even though the slider still changes stored state.
        root.alphaBufferAvailable = true;
        root.rendererSupportsWindowTransparency = true;
        theme().profile = ShellTheme.Balanced;
        theme().glassOpacity = 0.42;
        tryCompare(root, "windowTransparencyEnabled", true);
        tryCompare(ground, "fillOpacity", 0.42);
        compare(root.color, Qt.rgba(0, 0, 0, 0));

        // Functional chrome blends in color but stays opaque, matching the
        // existing render-site contrast bed instead of placing text directly
        // on an unknown desktop color.
        theme().surfaceOpacity = 0.45;
        tryVerify(function () {
            return !Qt.colorEqual(toolbar.background.color, theme().panel);
        });
        compare(toolbar.background.color.a, 1.0);

        // Reduced motion removes persistence only; a still material remains
        // available. High contrast and the Off profile select the opaque
        // fallback, as does a renderer that cannot preserve alpha.
        theme().reducedMotion = true;
        compare(root.windowTransparencyEnabled, true);
        theme().highContrast = true;
        compare(root.windowTransparencyEnabled, false);
        compare(ground.fillOpacity, 1.0);
        compare(root.color, theme().background);

        theme().highContrast = false;
        theme().profile = ShellTheme.Off;
        compare(root.windowTransparencyEnabled, false);
        compare(ground.fillOpacity, 1.0);

        theme().profile = ShellTheme.Balanced;
        root.alphaBufferAvailable = false;
        root.rendererSupportsWindowTransparency = true;
        compare(root.windowTransparencyAvailable, false);
        compare(root.windowTransparencyEnabled, false);
        compare(ground.fillOpacity, 1.0);
        compare(root.color, theme().background);

        root.alphaBufferAvailable = true;
        root.rendererSupportsWindowTransparency = false;
        compare(root.windowTransparencyAvailable, false);
        compare(root.windowTransparencyEnabled, false);
        compare(ground.fillOpacity, 1.0);
        compare(root.color, theme().background);
    }

    function test_pointerFocusIndicatorsSurviveEffectsOff() {
        requireWindowActivation();
        theme().profile = ShellTheme.Off;
        const layer = child("presentationLayer");
        tryVerify(function () {
            return !layer.active;
        });

        // Button chrome: the focus ring is the accent border, and it must
        // differ from the resting border so focus is unambiguous.
        verify(!Qt.colorEqual(theme().accent, theme().border));
        // The refresh action is always enabled; a disabled control cannot
        // take focus.
        const button = child("refreshButton");
        mouseClick(button, button.width / 2, button.height / 2, Qt.LeftButton);
        tryVerify(function () {
            return button.activeFocus;
        });
        const buttonBed = button.background;
        verify(buttonBed !== null && buttonBed !== undefined);
        tryVerify(function () {
            return Qt.colorEqual(buttonBed.border.color, theme().accent);
        });

        // Field chrome: same contract on the shared line edit.
        const field = child("filterField");
        mouseClick(field, field.width / 2, field.height / 2, Qt.LeftButton);
        tryVerify(function () {
            return field.activeFocus;
        });
        tryVerify(function () {
            return Qt.colorEqual(field.background.border.color, theme().accent);
        });
        tryVerify(function () {
            return Qt.colorEqual(buttonBed.border.color, theme().border);
        });

        // View focus: the pane frame's stroke turns accent while a
        // directory view owns focus.
        const list = child("directoryList");
        clickRow(1, Qt.LeftButton, Qt.NoModifier);
        tryVerify(function () {
            return list.activeFocus;
        });
        const frame = child("paneFrame");
        tryVerify(function () {
            return Qt.colorEqual(frame.strokeColor, theme().accent);
        });
        field.forceActiveFocus();
        tryVerify(function () {
            return Qt.colorEqual(frame.strokeColor, theme().border);
        });
    }

    function test_focusTraversalHasNoDeadEnd() {
        requireWindowActivation();
        const field = child("filterField");
        field.forceActiveFocus();
        tryVerify(function () {
            return field.activeFocus;
        });

        // Walk the tab chain a bounded number of steps: focus must move,
        // never land nowhere, and revisit a surface within the bound —
        // a dead end or an escape hatch both fail here.
        const visited = [];
        let revisited = false;
        for (let step = 0; step < 40 && !revisited; ++step) {
            keyClick(Qt.Key_Tab);
            const focused = testCase.shellWindow.activeFocusItem;
            verify(focused !== null, "tab traversal must never drop focus");
            if (visited.indexOf(focused) !== -1) {
                revisited = true;
            } else {
                visited.push(focused);
            }
        }
        verify(revisited, "tab traversal must cycle instead of wandering off");
        verify(visited.length >= 4, "tab traversal must reach several surfaces");
    }

    function test_reducedMotionSuppressesMotionAndKeepsLayout() {
        const layer = child("presentationLayer");
        verify(layer.motionDurationMs > 0);

        const observed = ["pathNavigator", "filterField", "statusMessageText", "directoryList"];
        const before = [];
        for (let index = 0; index < observed.length; ++index) {
            const item = child(observed[index]);
            const mapped = item.mapToItem(testCase.shellWindow.contentItem, 0, 0);
            before.push({
                "x": mapped.x,
                "y": mapped.y,
                "width": item.width,
                "height": item.height
            });
        }

        theme().reducedMotion = true;
        compare(theme().effectivePersistence, 0);
        compare(layer.motionDurationMs, 0);
        // The stored preference the slider shows is untouched.
        verify(theme().persistence > 0);

        waitForRendering(testCase.shellWindow.contentItem);
        for (let index = 0; index < observed.length; ++index) {
            const item = child(observed[index]);
            const mapped = item.mapToItem(testCase.shellWindow.contentItem, 0, 0);
            compare(mapped.x, before[index].x);
            compare(mapped.y, before[index].y);
            compare(item.width, before[index].width);
            compare(item.height, before[index].height);
        }
    }

    function test_effectsOffShellStaysUsable() {
        requireWindowActivation();
        // Pin a known usable geometry before interacting. This audit clicks a
        // specific row, which lands on the intended item only when the window
        // carries the size it asked for; a compositor that seats the window at
        // a size the client never requested — a tiling layout does — would put
        // the click on empty space and read an empty selection as a product
        // failure. resizeShell skips with the constraint named when the
        // platform will not apply the size, so a surface the gate could not
        // obtain is a named non-green here rather than a false failure.
        resizeShell(1100, 720);
        theme().profile = ShellTheme.Off;
        const layer = child("presentationLayer");
        compare(theme().effectiveBloomCore, 0);
        compare(theme().effectiveBloomWide, 0);
        compare(theme().effectiveScanline, 0);
        compare(theme().effectiveVignette, 0);
        compare(theme().effectivePersistence, 0);
        compare(theme().effectiveDeepField, 0);
        tryVerify(function () {
            return !layer.active;
        });

        // Selection stays legible without the effect layer: the beds and
        // inks are palette values, not pipeline output. Unselected rows are
        // transparent over the pane ground, so the ground pair measures the
        // background sheet — with effects off the deep-field ramps are gone
        // and the sheet is the whole bed.
        clickRow(1, Qt.LeftButton, Qt.NoModifier);
        compare(selectedRows(), [1]);
        verify(!Qt.colorEqual(theme().selectionBed, theme().background), "the selection bed must differ from the pane ground without effects");
        verify(contrastRatio(theme().text, theme().selectionBed) >= 4.5);
        verify(contrastRatio(theme().text, theme().background) >= 4.5);

        // The keyboard surfaces stay reachable: palette open and dismiss.
        const list = child("directoryList");
        list.forceActiveFocus();
        keySequence("Ctrl+Shift+P");
        const palette = child("commandPalette");
        tryVerify(function () {
            return palette.opened;
        });
        keyClick(Qt.Key_Escape);
        tryVerify(function () {
            return !palette.opened;
        });
    }

    function test_boundedGlowKeepsOneCurrentSelectionSource() {
        requireWindowActivation();
        const layer = child("presentationLayer");
        const tabGlow = child("tabGlow-0");
        const paneGlow = child("paneGlow-0");

        // This software entry proves the fallback boundary rather than
        // pretending it rendered a halo: the semantic outlines remain, but
        // no context marker can emit without the existing pipeline.
        verify(layer.softwareBackend);
        verify(!layer.contextGlowAvailable);
        verify(!tabGlow.glowEmitting);
        verify(!paneGlow.glowEmitting);

        // Only the current selected row requests the selection marker. A
        // second selection retains its semantic bed but cannot add another
        // glow source, so the source count is bounded independently of the
        // number of selected entries.
        clickRow(1, Qt.LeftButton, Qt.NoModifier);
        clickRow(2, Qt.LeftButton, Qt.ControlModifier);
        compare(selectedRows(), [1, 2]);
        const first = child("entryGlow-1");
        const current = child("entryGlow-2");
        verify(!first.selected);
        verify(current.selected);
        verify(current.requestedTreatmentCount >= 1);
        compare(current.compositionLevel, 1);
        verify(!first.glowEmitting && !current.glowEmitting);

        // Off is stronger than a dimmer treatment: no pipeline, no emitter.
        theme().profile = ShellTheme.Off;
        tryVerify(function () {
            return !layer.active && !layer.contextGlowAvailable;
        });
        verify(!tabGlow.glowEmitting && !paneGlow.glowEmitting && !current.glowEmitting);
    }

    function contrastRatio(first, second) {
        function linear(channel) {
            return channel <= 0.04045 ? channel / 12.92 : Math.pow((channel + 0.055) / 1.055, 2.4);
        }
        function luminance(color) {
            return (0.2126 * linear(color.r)) + (0.7152 * linear(color.g)) + (0.0722 * linear(color.b));
        }
        const bright = Math.max(luminance(first), luminance(second));
        const dark = Math.min(luminance(first), luminance(second));
        return (bright + 0.05) / (dark + 0.05);
    }

    function countRealizedRows(total) {
        let realized = 0;
        for (let index = 0; index < total; ++index) {
            if (findChild(testCase.shellWindow.contentItem, "entryRow-" + index) !== null) {
                ++realized;
            }
        }
        return realized;
    }

    function countDescendants(item) {
        let total = item.children.length;
        for (let index = 0; index < item.children.length; ++index) {
            total += countDescendants(item.children[index]);
        }
        return total;
    }

    function test_largeDirectoryStaysVirtualizedWithFlatEffectCost() {
        const layer = child("presentationLayer");
        const wells = child("wellMaskLayer");
        const layerCostBefore = countDescendants(layer);
        const mirrorsBefore = wells.mirrorCreationCount;
        const entryCount = testCase.representativeLargeDirectoryEntries;

        populateRows(entryCount);
        testCase.fakeModel.resetTelemetry();

        // The list realizes a viewport of delegates, not the directory.
        const list = child("directoryList");
        const listViewportRows = Math.ceil(list.height / theme().rowHeight);
        const listWorkBound = listViewportRows * 8 + 24;
        verify(entryCount >= listWorkBound * 10, "the fixture must remain at least ten viewports beyond the list work bound");
        const realizedRows = countRealizedRows(entryCount);
        verify(realizedRows > 0);
        verify(realizedRows <= listWorkBound, "the list must keep work at its viewport bound of " + listWorkBound + ", realized " + realizedRows);

        // Jumping to the far end stays a viewport-sized operation.
        list.positionViewAtEnd();
        waitForRendering(testCase.shellWindow.contentItem);
        verify(countRealizedRows(entryCount) <= listWorkBound, "the list end jump must keep work at the viewport bound");

        // The grid virtualizes the same directory.
        testCase.shellWindow.gridMode = true;
        const grid = child("directoryGrid");
        tryCompare(grid, "count", entryCount);
        waitForRendering(testCase.shellWindow.contentItem);
        const gridColumns = Math.max(1, Math.floor(grid.width / theme().gridCellWidth));
        const gridViewportRows = Math.ceil(grid.height / theme().gridCellHeight);
        const gridWorkBound = gridColumns * (gridViewportRows + 1) * 5;
        verify(entryCount >= gridWorkBound * 10, "the fixture must remain at least ten viewports beyond the grid work bound");
        const realizedCells = realizedGridCellCount(entryCount);
        verify(realizedCells > 0);
        verify(realizedCells <= gridWorkBound, "the grid must keep work at its viewport bound of " + gridWorkBound + ", realized " + realizedCells);

        // Delegates schedule thumbnails only while virtualized into the
        // viewport. Jumping across this representative directory must release
        // the abandoned batch before it asks for the far batch: cancellation
        // is part of keeping thumbnail work proportional to the view, not the
        // listing's total size.
        tryVerify(function () {
            return testCase.fakeModel.requestThumbnailCalls > 0;
        });
        const initialThumbnailRequests = testCase.fakeModel.requestThumbnailCalls;
        verify(initialThumbnailRequests <= gridWorkBound, "initial thumbnail requests must stay viewport-sized, requested " + initialThumbnailRequests);
        grid.positionViewAtEnd();
        waitForRendering(testCase.shellWindow.contentItem);
        tryVerify(function () {
            return testCase.fakeModel.releaseThumbnailCalls > 0;
        });
        verify(testCase.fakeModel.requestThumbnailCalls > initialThumbnailRequests, "the far viewport must schedule its own thumbnails");
        verify(testCase.fakeModel.requestThumbnailCalls <= gridWorkBound * 3, "thumbnail scheduling must remain bounded across the end jump, requested " + testCase.fakeModel.requestThumbnailCalls);
        testCase.shellWindow.gridMode = false;

        // The effect layer's structure is independent of entry count, and
        // the protected-well registry scales with realized thumbnails at
        // most — never with the directory.
        compare(countDescendants(layer), layerCostBefore);
        verify(wells.mirrorCreationCount - mirrorsBefore <= gridWorkBound, "well mirrors must track the viewport, not the directory");

        populateRows(4);
    }
}
