// The curated color families the shell ships with.
//
// The values are the accepted visual-identity decisions for each family,
// derived so that every role clears its contrast floor against the surfaces
// it renders on, directory ink stays measurably chromatic, and the selection
// bed remains distinct from the ground in every family.
#include "theme_palettes.hpp"

#include <QVector>

namespace odysea::app {

namespace {

[[nodiscard]] QVector<ShellPalette> buildPalettes() {
    QVector<ShellPalette> palettes;

    {
        ShellPalette p;
        p.id = QStringLiteral("odyssey-default");
        p.light = false;
        p.sheet = QColor(0x04, 0x1A, 0x0A);
        p.deep = QColor(0x01, 0x0C, 0x06);
        p.well = QColor(0x10, 0x26, 0x16);
        p.inset = QColor(0x07, 0x1D, 0x0E);
        p.frame = QColor(0x12, 0x30, 0x18);
        p.text = QColor(0x92, 0xE2, 0xB9);
        p.muted = QColor(0x67, 0xAC, 0x8B);
        p.faint = QColor(0x4B, 0x88, 0x69);
        p.dir = QColor(0x60, 0xD5, 0xDA);
        p.link = QColor(0x5F, 0xA8, 0xD8);
        p.meta = QColor(0x51, 0xAC, 0xAE);
        p.accent = QColor(0xD8, 0xB0, 0x30);
        p.selectionBed = QColor(0x00, 0x3A, 0x3A);
        p.selectionInk = QColor(0xB6, 0xF5, 0xD4);
        p.match = QColor(0x3A, 0x32, 0x10);
        p.focus = QColor(0x40, 0xC8, 0x98);
        p.icon = QColor(0x41, 0x76, 0x5B);
        p.danger = QColor(0xE7, 0x58, 0x53);
        p.dangerHigh = QColor(0xFC, 0x76, 0x6E);
        p.warning = QColor(0xC0, 0x94, 0x18);
        p.success = QColor(0x38, 0xB0, 0x48);
        palettes.append(p);
    }
    {
        ShellPalette p;
        p.id = QStringLiteral("odyssey-amber");
        p.light = false;
        p.sheet = QColor(0x10, 0x0A, 0x00);
        p.deep = QColor(0x0C, 0x08, 0x00);
        p.well = QColor(0x1A, 0x14, 0x04);
        p.inset = QColor(0x17, 0x11, 0x02);
        p.frame = QColor(0x2C, 0x1E, 0x00);
        p.text = QColor(0xFA, 0xDE, 0x84);
        p.muted = QColor(0xB9, 0xA0, 0x51);
        p.faint = QColor(0x7D, 0x66, 0x21);
        p.dir = QColor(0xB4, 0xE8, 0x8C);
        p.link = QColor(0x53, 0x7D, 0xCE);
        p.meta = QColor(0x90, 0xAE, 0x66);
        p.accent = QColor(0xC8, 0x98, 0x10);
        p.selectionBed = QColor(0x01, 0x23, 0x00);
        p.selectionInk = QColor(0xFF, 0xE8, 0x8D);
        p.match = QColor(0x30, 0x26, 0x00);
        p.focus = QColor(0xE8, 0xB8, 0x20);
        p.icon = QColor(0x7A, 0x64, 0x20);
        p.danger = QColor(0xDD, 0x4B, 0x4A);
        p.dangerHigh = QColor(0xEE, 0x58, 0x54);
        p.warning = QColor(0xC8, 0x98, 0x10);
        p.success = QColor(0x90, 0xB0, 0x20);
        palettes.append(p);
    }
    {
        ShellPalette p;
        p.id = QStringLiteral("odyssey-graphite");
        p.light = false;
        p.sheet = QColor(0x16, 0x18, 0x1C);
        p.deep = QColor(0x0E, 0x10, 0x13);
        p.well = QColor(0x20, 0x23, 0x27);
        p.inset = QColor(0x1D, 0x1F, 0x23);
        p.frame = QColor(0x22, 0x26, 0x30);
        p.text = QColor(0xDC, 0xE0, 0xE6);
        p.muted = QColor(0x9C, 0xA1, 0xAA);
        p.faint = QColor(0x6C, 0x74, 0x81);
        p.dir = QColor(0x9E, 0xE4, 0xEE);
        p.link = QColor(0x6A, 0x9A, 0xD0);
        p.meta = QColor(0x7C, 0xA9, 0xBB);
        p.accent = QColor(0xD6, 0xB4, 0x61);
        p.selectionBed = QColor(0x00, 0x2E, 0x39);
        p.selectionInk = QColor(0xE6, 0xEA, 0xF0);
        p.match = QColor(0x44, 0x40, 0x1C);
        p.focus = QColor(0xC0, 0xC4, 0xCC);
        p.icon = QColor(0x70, 0x72, 0x74);
        p.danger = QColor(0xD1, 0x6A, 0x72);
        p.dangerHigh = QColor(0xE1, 0x79, 0x83);
        p.warning = QColor(0xD4, 0xB5, 0x6A);
        p.success = QColor(0x8A, 0xBA, 0x78);
        palettes.append(p);
    }
    {
        ShellPalette p;
        p.id = QStringLiteral("odyssey-aurora");
        p.light = false;
        p.sheet = QColor(0x06, 0x10, 0x1F);
        p.deep = QColor(0x03, 0x0A, 0x14);
        p.well = QColor(0x0F, 0x1A, 0x2A);
        p.inset = QColor(0x0C, 0x17, 0x26);
        p.frame = QColor(0x16, 0x26, 0x3E);
        p.text = QColor(0xF4, 0xFC, 0xFF);
        p.muted = QColor(0xA6, 0xB4, 0xC9);
        p.faint = QColor(0x5B, 0x70, 0x8C);
        p.dir = QColor(0xB4, 0xFF, 0xFF);
        p.link = QColor(0x5D, 0xB4, 0xFF);
        p.meta = QColor(0x85, 0xBE, 0xCB);
        p.accent = QColor(0xFF, 0xD4, 0x5D);
        p.selectionBed = QColor(0x00, 0x2A, 0x2D);
        p.selectionInk = QColor(0xFE, 0xFF, 0xFF);
        p.match = QColor(0x4A, 0x3D, 0x12);
        p.focus = QColor(0x5D, 0xB4, 0xFF);
        p.icon = QColor(0x6C, 0x74, 0x80);
        p.danger = QColor(0xFF, 0x5D, 0x7A);
        p.dangerHigh = QColor(0xFF, 0x5D, 0x7A);
        p.warning = QColor(0xFF, 0xD4, 0x5D);
        p.success = QColor(0x5D, 0xFF, 0xA6);
        palettes.append(p);
    }
    {
        ShellPalette p;
        p.id = QStringLiteral("odyssey-parchment-light");
        p.light = true;
        p.sheet = QColor(0xF6, 0xEF, 0xE0);
        p.deep = QColor(0xFC, 0xF7, 0xEC);
        p.well = QColor(0xE7, 0xE0, 0xD1);
        p.inset = QColor(0xEC, 0xE5, 0xD6);
        p.frame = QColor(0xDD, 0xD0, 0xB6);
        p.text = QColor(0x2C, 0x22, 0x15);
        p.muted = QColor(0x5A, 0x4E, 0x3C);
        p.faint = QColor(0x80, 0x75, 0x5F);
        p.dir = QColor(0x53, 0x0C, 0x0B);
        p.link = QColor(0x32, 0x66, 0xA1);
        p.meta = QColor(0x6D, 0x43, 0x3B);
        p.accent = QColor(0x9A, 0x6A, 0x16);
        p.selectionBed = QColor(0xF6, 0xCF, 0xBD);
        p.selectionInk = QColor(0x25, 0x1B, 0x0E);
        p.match = QColor(0xE8, 0xD4, 0x9C);
        p.focus = QColor(0xAE, 0x69, 0x1C);
        p.icon = QColor(0x2C, 0x22, 0x15);
        p.danger = QColor(0xAE, 0x3E, 0x35);
        p.dangerHigh = QColor(0x9E, 0x38, 0x30);
        p.warning = QColor(0x89, 0x5A, 0x00);
        p.success = QColor(0x4C, 0x6E, 0x16);
        palettes.append(p);
    }
    {
        ShellPalette p;
        p.id = QStringLiteral("odyssey-slate-light");
        p.light = true;
        p.sheet = QColor(0xEC, 0xF0, 0xF8);
        p.deep = QColor(0xF4, 0xF6, 0xFC);
        p.well = QColor(0xDD, 0xE1, 0xE9);
        p.inset = QColor(0xE2, 0xE6, 0xEE);
        p.frame = QColor(0xB0, 0xBC, 0xE0);
        p.text = QColor(0x15, 0x1F, 0x38);
        p.muted = QColor(0x39, 0x46, 0x69);
        p.faint = QColor(0x60, 0x70, 0xA0);
        p.dir = QColor(0x3D, 0x0F, 0x44);
        p.link = QColor(0x24, 0x54, 0xB8);
        p.meta = QColor(0x51, 0x3E, 0x62);
        p.accent = QColor(0x83, 0x5B, 0x00);
        p.selectionBed = QColor(0xD9, 0xD4, 0xF8);
        p.selectionInk = QColor(0x0F, 0x17, 0x30);
        p.match = QColor(0xE4, 0xD8, 0xA8);
        p.focus = QColor(0x38, 0x60, 0xCC);
        p.icon = QColor(0x15, 0x1F, 0x38);
        p.danger = QColor(0xB2, 0x2C, 0x3C);
        p.dangerHigh = QColor(0xA6, 0x28, 0x38);
        p.warning = QColor(0x80, 0x5C, 0x10);
        p.success = QColor(0x24, 0x72, 0x3B);
        palettes.append(p);
    }

    return palettes;
}

[[nodiscard]] const QVector<ShellPalette>& allPalettes() {
    static const QVector<ShellPalette> palettes = buildPalettes();
    return palettes;
}

} // namespace

QString defaultShellPaletteId() {
    return allPalettes().first().id;
}

QStringList shellPaletteIds() {
    QStringList ids;
    ids.reserve(allPalettes().size());
    for (const ShellPalette& palette : allPalettes()) {
        ids.append(palette.id);
    }
    return ids;
}

const ShellPalette& shellPalette(const QString& id) {
    for (const ShellPalette& palette : allPalettes()) {
        if (palette.id == id) {
            return palette;
        }
    }
    return allPalettes().first();
}

} // namespace odysea::app
