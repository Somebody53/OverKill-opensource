#pragma once

#include "raylib.h"

// Crosshair emblem on a dark disc, drawn on the CPU so it needs no window.
inline Image makeLogoImage(int size) {
    const Color red{220, 60, 40, 255};
    auto s = [size](int v) { return v * size / 64; };
    Image icon = GenImageColor(size, size, BLANK);
    ImageDrawCircle(&icon, s(32), s(32), s(31), Color{30, 30, 40, 255});
    for (int r = s(20); r <= s(23); ++r) {
        ImageDrawCircleLines(&icon, s(32), s(32), r, red);
    }
    ImageDrawRectangle(&icon, s(4), s(31), s(14), s(3), red);
    ImageDrawRectangle(&icon, s(46), s(31), s(14), s(3), red);
    ImageDrawRectangle(&icon, s(31), s(4), s(3), s(14), red);
    ImageDrawRectangle(&icon, s(31), s(46), s(3), s(14), red);
    ImageDrawCircle(&icon, s(32), s(32), s(3), red);
    return icon;
}
