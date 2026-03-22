#pragma once

#include <algorithm>
#include <cstdint>
#include <vector>

namespace glitch {

// ── Chromatic Aberration ───────────────────────────────────────────────────────
// Mimics lens colour fringing by laterally shifting each colour channel
// by a different amount:
//
//   R channel  →  shifted LEFT  by `shift` pixels  (x - shift)
//   G channel  →  no shift
//   B channel  →  shifted RIGHT by `shift` pixels  (x + shift)
//   A channel  →  no shift  (taken from the original pixel)
//
// Out-of-bounds samples are clamped to the nearest edge pixel.
// Returns a new RGBA buffer — the source is left unchanged.
//
//  src    – packed RGBA source buffer (width * height * 4 bytes)
//  width  – image width  in pixels
//  height – image height in pixels
//  shift  – lateral displacement in pixels (try 4–20 for visible effect)
inline std::vector<uint8_t> chromatic_aberration(
    const uint8_t* src,
    int            width,
    int            height,
    int            shift)
{
    std::vector<uint8_t> dst(static_cast<std::size_t>(width * height * 4));

    // Helper: linear pixel index from (x, y), x clamped to [0, width-1]
    auto idx = [&](int x, int y) noexcept -> int {
        x = std::clamp(x, 0, width - 1);
        return (y * width + x) * 4;
    };

    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            const int d = (y * width + x) * 4;

            dst[d + 0] = src[idx(x - shift, y) + 0];  // R  (shifted left)
            dst[d + 1] = src[idx(x,          y) + 1];  // G  (no shift)
            dst[d + 2] = src[idx(x + shift, y) + 2];  // B  (shifted right)
            dst[d + 3] = src[idx(x,          y) + 3];  // A  (no shift)
        }
    }

    return dst;
}

} // namespace glitch
