#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

#include "stb_image.h"
#include "stb_image_write.h"

#include "pixel_sorter.h"
#include "chromatic_aberration.h"

static void usage(const char* prog) {
    std::fprintf(stderr,
        "Usage: %s <input> <output.png> [options]\n\n"
        "Options:\n"
        "  --sort               Sort every row's pixels by brightness (dark→bright)\n"
        "  --aberration <n>     Chromatic aberration: shift R/B channels by n pixels\n\n"
        "Examples:\n"
        "  %s photo.jpg out.png --sort\n"
        "  %s photo.jpg out.png --aberration 12\n"
        "  %s photo.jpg out.png --sort --aberration 8\n",
        prog, prog, prog, prog);
}

int main(int argc, char* argv[]) {
    if (argc < 3) {
        usage(argv[0]);
        return 1;
    }

    const char* input_path  = argv[1];
    const char* output_path = argv[2];

    bool do_sort       = false;
    int  aberr_shift   = 0;

    for (int i = 3; i < argc; ++i) {
        if (std::strcmp(argv[i], "--sort") == 0) {
            do_sort = true;
        } else if (std::strcmp(argv[i], "--aberration") == 0) {
            if (i + 1 >= argc) {
                std::fprintf(stderr, "error: --aberration requires a pixel value\n");
                return 1;
            }
            aberr_shift = std::atoi(argv[++i]);
        } else {
            std::fprintf(stderr, "error: unknown option '%s'\n", argv[i]);
            usage(argv[0]);
            return 1;
        }
    }

    // ── 1. Load image ────────────────────────────────────────────────────────
    // Force 4-channel RGBA so every code path works with a uniform layout.
    int width, height, original_channels;
    uint8_t* raw = stbi_load(input_path, &width, &height, &original_channels, 4);
    if (!raw) {
        std::fprintf(stderr, "error: could not load '%s': %s\n",
                     input_path, stbi_failure_reason());
        return 1;
    }
    std::printf("Loaded  : %s  (%d x %d, %d ch → forced RGBA)\n",
                input_path, width, height, original_channels);

    // Copy into a vector so we own the memory and can reassign it safely.
    std::vector<uint8_t> pixels(raw, raw + width * height * 4);
    stbi_image_free(raw);

    // ── 2. Pixel sort ────────────────────────────────────────────────────────
    // std::sort each scanline by perceptual luma (ITU-R BT.601):
    //   Y = 0.299 R + 0.587 G + 0.114 B
    // Pixels within a row are moved as whole RGBA units — alpha is preserved.
    if (do_sort) {
        std::printf("Effect  : pixel sort (sort rows by brightness)\n");
        glitch::sort_rows_by_brightness(pixels.data(), width, height);
    }

    // ── 3. Chromatic aberration ──────────────────────────────────────────────
    // Each colour channel is sampled from a slightly different x-coordinate:
    //   R  ← x - shift   (shifts red fringe to the left)
    //   G  ← x           (green stays centred)
    //   B  ← x + shift   (shifts blue fringe to the right)
    // Out-of-bounds reads are clamped to the nearest edge column.
    if (aberr_shift != 0) {
        std::printf("Effect  : chromatic aberration (shift = %d px)\n", aberr_shift);
        pixels = glitch::chromatic_aberration(
            pixels.data(), width, height, aberr_shift);
    }

    // ── 4. Save result ───────────────────────────────────────────────────────
    // stride = width * 4 bytes (tightly packed RGBA).
    const int stride = width * 4;
    if (!stbi_write_png(output_path, width, height, 4, pixels.data(), stride)) {
        std::fprintf(stderr, "error: could not write '%s'\n", output_path);
        return 1;
    }
    std::printf("Saved   : %s\n", output_path);

    return 0;
}
