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
        p.sheet = QColor(0x03, 0x14, 0x08);
        p.deep = QColor(0x01, 0x0C, 0x06);
        p.well = QColor(0x10, 0x26, 0x16);
        p.inset = QColor(0x07, 0x1D, 0x0E);
        p.frame = QColor(0x10, 0x28, 0x14);
        p.text = QColor(0x92, 0xE2, 0xB9);
        p.muted = QColor(0x67, 0xAC, 0x8B);
        p.faint = QColor(0x4B, 0x88, 0x69);
        p.dir = QColor(0x60, 0xD5, 0xDA);
        p.link = QColor(0x5F, 0xA8, 0xD8);
        p.meta = QColor(0x51, 0xAC, 0xAE);
        p.accent = QColor(0xD8, 0xB0, 0x30);
        p.selectionBed = QColor(0x00, 0x3A, 0x3A);
        p.selectionInk = QColor(0xB6, 0xF5, 0xD4);
        p.match = QColor(0x1C, 0x2C, 0x10);
        p.focus = QColor(0x40, 0xC8, 0x98);
        p.icon = QColor(0x40, 0x78, 0x60);
        p.danger = QColor(0xE7, 0x58, 0x53);
        p.dangerHigh = QColor(0xFC, 0x76, 0x6E);
        p.warning = QColor(0xC0, 0x94, 0x18);
        p.success = QColor(0x38, 0xB0, 0x48);
        palettes.append(p);
    }
    {
        ShellPalette p;
        p.id = QStringLiteral("odyssey");
        p.light = false;
        p.sheet = QColor(0x0C, 0x12, 0x24);
        p.deep = QColor(0x07, 0x0B, 0x18);
        p.well = QColor(0x11, 0x19, 0x32);
        p.inset = QColor(0x0F, 0x17, 0x2E);
        p.frame = QColor(0x1B, 0x24, 0x3E);
        p.text = QColor(0xE0, 0xE6, 0xF7);
        p.muted = QColor(0x9C, 0xA5, 0xBD);
        p.faint = QColor(0x75, 0x7F, 0x9A);
        p.dir = QColor(0x92, 0xDC, 0xE5);
        p.link = QColor(0x61, 0xAF, 0xEF);
        p.meta = QColor(0x6F, 0xAD, 0xBE);
        p.accent = QColor(0xE5, 0xC0, 0x7B);
        p.selectionBed = QColor(0x10, 0x2A, 0x4A);
        p.selectionInk = QColor(0xEE, 0xF3, 0xFF);
        p.match = QColor(0x4A, 0x40, 0x18);
        p.focus = QColor(0x86, 0xC1, 0xFF);
        p.icon = QColor(0x5F, 0x64, 0x73);
        p.danger = QColor(0xE0, 0x6B, 0x74);
        p.dangerHigh = QColor(0xFF, 0x8B, 0x92);
        p.warning = QColor(0xE5, 0xC0, 0x7B);
        p.success = QColor(0x98, 0xC3, 0x79);
        palettes.append(p);
    }
    {
        ShellPalette p;
        p.id = QStringLiteral("odyssey-midnight");
        p.light = false;
        p.sheet = QColor(0x03, 0x06, 0x14);
        p.deep = QColor(0x01, 0x03, 0x0D);
        p.well = QColor(0x0D, 0x10, 0x27);
        p.inset = QColor(0x0A, 0x0D, 0x21);
        p.frame = QColor(0x0C, 0x18, 0x38);
        p.text = QColor(0xEE, 0xF7, 0xFF);
        p.muted = QColor(0x9E, 0xAB, 0xD2);
        p.faint = QColor(0x58, 0x6A, 0xB0);
        p.dir = QColor(0x68, 0xDC, 0xF4);
        p.link = QColor(0x80, 0xA4, 0xFF);
        p.meta = QColor(0x81, 0x97, 0xBD);
        p.accent = QColor(0xFF, 0xD8, 0x70);
        p.selectionBed = QColor(0x10, 0x24, 0x42);
        p.selectionInk = QColor(0xF0, 0xF4, 0xFF);
        p.match = QColor(0x3C, 0x3C, 0x14);
        p.focus = QColor(0x58, 0x82, 0xFF);
        p.icon = QColor(0x57, 0x7F, 0xA0);
        p.danger = QColor(0xF0, 0x60, 0x70);
        p.dangerHigh = QColor(0xFF, 0x88, 0x98);
        p.warning = QColor(0xE8, 0xC0, 0x50);
        p.success = QColor(0x50, 0xD8, 0xA0);
        palettes.append(p);
    }
    {
        ShellPalette p;
        p.id = QStringLiteral("odyssey-harvest");
        p.light = false;
        p.sheet = QColor(0x0E, 0x0A, 0x00);
        p.deep = QColor(0x09, 0x07, 0x00);
        p.well = QColor(0x1B, 0x16, 0x04);
        p.inset = QColor(0x17, 0x12, 0x02);
        p.frame = QColor(0x2A, 0x20, 0x00);
        p.text = QColor(0xFE, 0xF2, 0xA2);
        p.muted = QColor(0xBB, 0xAD, 0x67);
        p.faint = QColor(0x87, 0x75, 0x35);
        p.dir = QColor(0xB0, 0xCC, 0x48);
        p.link = QColor(0x78, 0x98, 0xE0);
        p.meta = QColor(0xA5, 0xAE, 0x57);
        p.accent = QColor(0xF0, 0xBE, 0x38);
        p.selectionBed = QColor(0x12, 0x30, 0x00);
        p.selectionInk = QColor(0xFA, 0xF4, 0xCC);
        p.match = QColor(0x3A, 0x2E, 0x00);
        p.focus = QColor(0xD8, 0xA0, 0x20);
        p.icon = QColor(0x7A, 0x6A, 0x30);
        p.danger = QColor(0xD0, 0x40, 0x40);
        p.dangerHigh = QColor(0xEC, 0x64, 0x60);
        p.warning = QColor(0xD8, 0xA0, 0x20);
        p.success = QColor(0x8A, 0xAA, 0x30);
        palettes.append(p);
    }
    {
        ShellPalette p;
        p.id = QStringLiteral("odyssey-lagoon");
        p.light = false;
        p.sheet = QColor(0x07, 0x14, 0x18);
        p.deep = QColor(0x04, 0x0E, 0x12);
        p.well = QColor(0x11, 0x23, 0x29);
        p.inset = QColor(0x0E, 0x1D, 0x22);
        p.frame = QColor(0x1A, 0x30, 0x40);
        p.text = QColor(0xD2, 0xF8, 0xFF);
        p.muted = QColor(0x96, 0xBC, 0xC7);
        p.faint = QColor(0x67, 0x91, 0x9E);
        p.dir = QColor(0x48, 0xD8, 0xE4);
        p.link = QColor(0x58, 0xA8, 0xEE);
        p.meta = QColor(0x78, 0xAA, 0xB4);
        p.accent = QColor(0xE8, 0xD8, 0x68);
        p.selectionBed = QColor(0x08, 0x35, 0x3A);
        p.selectionInk = QColor(0xE4, 0xF6, 0xFC);
        p.match = QColor(0x3A, 0x4A, 0x1A);
        p.focus = QColor(0x3A, 0xC4, 0xE0);
        p.icon = QColor(0x5F, 0x8C, 0x90);
        p.danger = QColor(0xE8, 0x58, 0x78);
        p.dangerHigh = QColor(0xFF, 0x78, 0x98);
        p.warning = QColor(0xD0, 0xBC, 0x48);
        p.success = QColor(0x38, 0xC8, 0x80);
        palettes.append(p);
    }
    {
        ShellPalette p;
        p.id = QStringLiteral("odyssey-plasma");
        p.light = false;
        p.sheet = QColor(0x14, 0x06, 0x1A);
        p.deep = QColor(0x0C, 0x03, 0x10);
        p.well = QColor(0x23, 0x10, 0x2B);
        p.inset = QColor(0x1E, 0x0D, 0x25);
        p.frame = QColor(0x2C, 0x16, 0x38);
        p.text = QColor(0xFD, 0xEC, 0xFF);
        p.muted = QColor(0xBA, 0xA5, 0xC6);
        p.faint = QColor(0x86, 0x69, 0x98);
        p.dir = QColor(0x97, 0xDC, 0xEE);
        p.link = QColor(0xAA, 0xAE, 0xF4);
        p.meta = QColor(0xA9, 0x91, 0xB4);
        p.accent = QColor(0xEF, 0xCD, 0x86);
        p.selectionBed = QColor(0x2A, 0x17, 0x40);
        p.selectionInk = QColor(0xF7, 0xEF, 0xFB);
        p.match = QColor(0x46, 0x34, 0x1A);
        p.focus = QColor(0xD4, 0x6F, 0xF0);
        p.icon = QColor(0x7C, 0x6B, 0x8A);
        p.danger = QColor(0xF0, 0x68, 0x8E);
        p.dangerHigh = QColor(0xF7, 0x8C, 0xAE);
        p.warning = QColor(0xDF, 0xAE, 0x5C);
        p.success = QColor(0x7A, 0xD8, 0x9A);
        palettes.append(p);
    }
    {
        ShellPalette p;
        p.id = QStringLiteral("odyssey-borealis");
        p.light = false;
        p.sheet = QColor(0x07, 0x16, 0x0F);
        p.deep = QColor(0x03, 0x0D, 0x08);
        p.well = QColor(0x11, 0x27, 0x1C);
        p.inset = QColor(0x0E, 0x20, 0x17);
        p.frame = QColor(0x14, 0x30, 0x1F);
        p.text = QColor(0xE9, 0xFF, 0xF0);
        p.muted = QColor(0x9E, 0xBB, 0xAA);
        p.faint = QColor(0x5E, 0x87, 0x70);
        p.dir = QColor(0x84, 0xEC, 0xDC);
        p.link = QColor(0x86, 0xD4, 0xF0);
        p.meta = QColor(0x78, 0xA4, 0x8D);
        p.accent = QColor(0xEC, 0xDC, 0x86);
        p.selectionBed = QColor(0x00, 0x38, 0x2F);
        p.selectionInk = QColor(0xEE, 0xFA, 0xF1);
        p.match = QColor(0x3D, 0x3E, 0x10);
        p.focus = QColor(0x57, 0xE3, 0x9A);
        p.icon = QColor(0x58, 0x7A, 0x68);
        p.danger = QColor(0xF0, 0x68, 0x7E);
        p.dangerHigh = QColor(0xF7, 0x8C, 0x9C);
        p.warning = QColor(0xD7, 0xC9, 0x5C);
        p.success = QColor(0x57, 0xE3, 0x9A);
        palettes.append(p);
    }
    {
        ShellPalette p;
        p.id = QStringLiteral("odyssey-crimson");
        p.light = false;
        p.sheet = QColor(0x1A, 0x08, 0x08);
        p.deep = QColor(0x10, 0x04, 0x04);
        p.well = QColor(0x2B, 0x14, 0x14);
        p.inset = QColor(0x25, 0x10, 0x10);
        p.frame = QColor(0x36, 0x1A, 0x1C);
        p.text = QColor(0xFF, 0xE8, 0xE8);
        p.muted = QColor(0xC3, 0xA2, 0xA4);
        p.faint = QColor(0x98, 0x68, 0x6C);
        p.dir = QColor(0x8E, 0xD8, 0xD0);
        p.link = QColor(0xA0, 0xC8, 0xE4);
        p.meta = QColor(0xAC, 0x85, 0x88);
        p.accent = QColor(0xEC, 0xCE, 0x86);
        p.selectionBed = QColor(0x3A, 0x20, 0x20);
        p.selectionInk = QColor(0xFC, 0xEE, 0xEE);
        p.match = QColor(0x44, 0x34, 0x10);
        p.focus = QColor(0xE8, 0x5C, 0x6A);
        p.icon = QColor(0x8A, 0x6F, 0x68);
        p.danger = QColor(0xEF, 0x60, 0x70);
        p.dangerHigh = QColor(0xF7, 0x88, 0x94);
        p.warning = QColor(0xD6, 0xAC, 0x5E);
        p.success = QColor(0x86, 0xC9, 0x8C);
        palettes.append(p);
    }
    {
        ShellPalette p;
        p.id = QStringLiteral("odyssey-fuchsia");
        p.light = false;
        p.sheet = QColor(0x16, 0x0A, 0x14);
        p.deep = QColor(0x0E, 0x06, 0x10);
        p.well = QColor(0x26, 0x16, 0x24);
        p.inset = QColor(0x20, 0x12, 0x1E);
        p.frame = QColor(0x40, 0x18, 0x40);
        p.text = QColor(0xFE, 0xE2, 0xF6);
        p.muted = QColor(0xC2, 0x94, 0xB5);
        p.faint = QColor(0xA4, 0x66, 0x86);
        p.dir = QColor(0x68, 0xD8, 0xE8);
        p.link = QColor(0x88, 0xA0, 0xF4);
        p.meta = QColor(0xAD, 0x78, 0x9D);
        p.accent = QColor(0xEC, 0xC8, 0x60);
        p.selectionBed = QColor(0x24, 0x30, 0x44);
        p.selectionInk = QColor(0xFC, 0xE8, 0xF8);
        p.match = QColor(0x3A, 0x2E, 0x10);
        p.focus = QColor(0xE8, 0x52, 0xC0);
        p.icon = QColor(0x8A, 0x6F, 0x7C);
        p.danger = QColor(0xF0, 0x58, 0x70);
        p.dangerHigh = QColor(0xFF, 0x7A, 0x90);
        p.warning = QColor(0xD4, 0xAA, 0x44);
        p.success = QColor(0x56, 0xCC, 0x8C);
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

[[nodiscard]] const QList<AccentPreset>& allAccentPresets() {
    static const QList<AccentPreset> presets{{.id = QStringLiteral("tideglass"),
                                              .name = QStringLiteral("Tideglass"),
                                              .accent = QColor(0xD8, 0xB0, 0x30)},
                                             {.id = QStringLiteral("beacon"),
                                              .name = QStringLiteral("Beacon"),
                                              .accent = QColor(0x4D, 0xB8, 0xFF)},
                                             {.id = QStringLiteral("ember"),
                                              .name = QStringLiteral("Ember"),
                                              .accent = QColor(0xEF, 0x8A, 0x48)},
                                             {.id = QStringLiteral("orchid"),
                                              .name = QStringLiteral("Orchid"),
                                              .accent = QColor(0xCF, 0x8C, 0xF4)},
                                             {.id = QStringLiteral("verdant"),
                                              .name = QStringLiteral("Verdant"),
                                              .accent = QColor(0x72, 0xCF, 0x8A)}};
    return presets;
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

QString defaultAccentPresetId() {
    return allAccentPresets().first().id;
}

QList<AccentPreset> shellAccentPresets() {
    return allAccentPresets();
}

const AccentPreset& shellAccentPreset(const QString& id) {
    for (const AccentPreset& preset : allAccentPresets()) {
        if (preset.id == id) {
            return preset;
        }
    }
    return allAccentPresets().first();
}

} // namespace odysea::app
