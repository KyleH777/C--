# glitch_art

A C++20 command-line tool for applying **pixel-sort** and **chromatic aberration** glitch effects to images.

## Features

| Effect | What it does |
|---|---|
| **Pixel Sort** | Sorts every row of pixels by perceptual brightness (dark → bright), creating the classic "melting" glitch look |
| **Chromatic Aberration** | Shifts the R channel left and the B channel right by a configurable number of pixels, mimicking lens colour fringing |

Both effects can be applied independently or chained.

## How It Works

### 1. Loading image data (`stb_image.h`)

```cpp
// Force 4-channel RGBA regardless of the source format (JPEG, PNG, BMP, …)
int width, height, original_channels;
uint8_t* raw = stbi_load("photo.jpg", &width, &height, &original_channels, 4);
// raw now points to width * height * 4 bytes: R G B A R G B A …
std::vector<uint8_t> pixels(raw, raw + width * height * 4);
stbi_image_free(raw);
```

`stb_image` is a single public-domain header that decodes JPEG, PNG, BMP, TGA, GIF, and more into a flat byte array. Requesting 4 channels normalises every format to RGBA so the rest of the code never has to branch on channel count.

---

### 2. Pixel Sorting (`include/pixel_sorter.h`)

```cpp
// Treat each group of 4 bytes as a whole RGBA pixel struct
struct Pixel { uint8_t r, g, b, a; };

for (int y = 0; y < height; ++y) {
    Pixel* row = reinterpret_cast<Pixel*>(data + y * width * 4);
    std::sort(row, row + width, [](const Pixel& p, const Pixel& q) {
        // Perceptual luma (ITU-R BT.601)
        float lp = 0.299f*p.r + 0.587f*p.g + 0.114f*p.b;
        float lq = 0.299f*q.r + 0.587f*q.g + 0.114f*q.b;
        return lp < lq;
    });
}
```

`reinterpret_cast<Pixel*>` lets `std::sort` move whole 4-byte pixels atomically — alpha is always carried with its RGB triplet.

---

### 3. Chromatic Aberration (`include/chromatic_aberration.h`)

```cpp
for (int y = 0; y < height; ++y) {
    for (int x = 0; x < width; ++x) {
        // R channel – sampled from x - shift (clamped to image boundary)
        dst[d + 0] = src[idx(x - shift, y) + 0];
        // G channel – no shift
        dst[d + 1] = src[idx(x,          y) + 1];
        // B channel – sampled from x + shift
        dst[d + 2] = src[idx(x + shift, y) + 2];
        // A channel – no shift
        dst[d + 3] = src[idx(x,          y) + 3];
    }
}
```

Each destination pixel reads its R, G, and B values from three different source columns. The clamped `idx()` helper prevents out-of-bounds reads at the image edges.

---

### 4. Saving the result (`stb_image_write.h`)

```cpp
// stride = width * 4 (tightly packed RGBA rows)
stbi_write_png("output.png", width, height, 4, pixels.data(), width * 4);
```

`stb_image_write` encodes the byte array back to a lossless PNG in one call.

---

## Project Structure

```
glitch_art/
├── CMakeLists.txt                  # C++20, FetchContent(stb)
├── include/
│   ├── pixel_sorter.h              # sort_rows_by_brightness()
│   └── chromatic_aberration.h      # chromatic_aberration()
└── src/
    ├── main.cpp                    # CLI: load → effect(s) → save
    └── stb_impl.cpp                # STB_IMAGE_IMPLEMENTATION (one TU only)
```

## Build

```bash
cd glitch_art
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc)
```

## Usage

```bash
# Pixel sort only
./build/glitch_art photo.jpg out.png --sort

# Chromatic aberration only (shift = 12 px)
./build/glitch_art photo.jpg out.png --aberration 12

# Chain both effects
./build/glitch_art photo.jpg out.png --sort --aberration 8
```
