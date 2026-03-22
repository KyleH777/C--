#pragma once

#include <algorithm>
#include <cstdint>

namespace glitch {

// ── Perceptual brightness (ITU-R BT.601 luma) ─────────────────────────────────
// Returns a value in [0, 255*max_weight] ≈ [0, 255].
inline float brightness(uint8_t r, uint8_t g, uint8_t b) noexcept {
    return 0.299f * r + 0.587f * g + 0.114f * b;
}

// ── Pixel Sorting ──────────────────────────────────────────────────────────────
// Sorts every row of an RGBA image in-place by ascending pixel brightness.
//
//  data   – packed RGBA byte array (width * height * 4 bytes)
//  width  – image width  in pixels
//  height – image height in pixels
//
// After calling this, each scanline is a left-dark → right-bright gradient
// built from the original pixels — the hallmark "pixel sort" glitch aesthetic.
inline void sort_rows_by_brightness(uint8_t* data, int width, int height) {
    // Reinterpret the flat byte array as an array of 4-byte RGBA structs so
    // std::sort can move whole pixels atomically.
    struct Pixel { uint8_t r, g, b, a; };

    for (int y = 0; y < height; ++y) {
        Pixel* row = reinterpret_cast<Pixel*>(data + y * width * 4);
        std::sort(row, row + width, [](const Pixel& p, const Pixel& q) noexcept {
            return brightness(p.r, p.g, p.b) < brightness(q.r, q.g, q.b);
        });
    }
}

} // namespace glitch
