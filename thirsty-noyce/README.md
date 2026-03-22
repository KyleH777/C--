# C++ Systems & Graphics Portfolio

A collection of three production-quality C++20 projects demonstrating real-time graphics, image processing, and high-throughput data pipelines. Each project is independently buildable, CI-tested on Linux, macOS, and Windows, and written to showcase deliberate engineering decisions — not just working code.

[![Particle Sim CI](https://github.com/KyleH777/C--/actions/workflows/particle_sim.yml/badge.svg)](https://github.com/KyleH777/C--/actions/workflows/particle_sim.yml)
[![Glitch Art CI](https://github.com/KyleH777/C--/actions/workflows/glitch_art.yml/badge.svg)](https://github.com/KyleH777/C--/actions/workflows/glitch_art.yml)
[![C++ Standard](https://img.shields.io/badge/C%2B%2B-20-blue.svg)](https://en.cppreference.com/w/cpp/20)
[![CMake](https://img.shields.io/badge/CMake-3.20%2B-064F8C.svg)](https://cmake.org/)
[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](LICENSE)

---

## Projects at a Glance

| Project | Domain | Key Techniques | Lines of C++ |
|---|---|---|---|
| [Particle Simulation](#-particle-simulation) | Real-time graphics | Batch rendering, parallel STL, fixed timestep | ~400 |
| [Glitch Art](#-glitch-art) | Image processing | Pixel sorting, chromatic aberration, CLI design | ~250 |
| [Tick Processor](#-tick-processor) | Systems / Finance | mmap I/O, MPMC queue, producer-consumer pipeline | ~600 |

---

# 🎮 Particle Simulation

> Real-time 2D particle fountain — up to **20,000 particles at a stable 60 FPS** with physics, boundary modes, and mouse-driven forces.

<!-- ============================================================
     DEMO GIF
     Replace the line below with a screen recording of the sim.
     Recommended tool: peek (Linux), LICEcap (Windows/macOS)
     Suggested size: 1280×720, cropped to ~800×450, ≤ 5 MB
     ============================================================ -->
![Particle Simulation Demo](assets/particle_demo.gif)

---

## Tech Stack

| Technology | Version | Role |
|---|---|---|
| **C++** | 20 | Language standard — `std::atomic`, `std::span`, `[[nodiscard]]` |
| **SFML** | 2.6.1 | Window management, input polling, GPU vertex submission |
| **CMake** | 3.20+ | Cross-platform build; `FetchContent` pulls SFML — no system install |
| **Intel TBB** | System | Backend for `std::execution::par_unseq` on Linux (optional) |
| **GitHub Actions** | — | CI matrix: Ubuntu · macOS · Windows × Debug · Release |

---

## Key Features

### Batch GPU Rendering — one draw call for all particles
The naive approach (`window.draw(circle)` per particle) issues one OpenGL state change per particle. At N = 1,000 that already saturates the CPU-GPU command bus. Instead, all particles are written into a **pre-allocated `std::vector<sf::Vertex>`** (4 vertices per particle) and submitted in a single call:

```cpp
// One GPU call — regardless of whether there are 100 or 20,000 particles
target.draw(m_vertices.data(), m_particles.size() * 4, sf::Quads, states);
```

The vertex buffer is `resize()`d **once** in the constructor to `maxParticles × 4`. `rebuildVertices()` becomes a pure write loop — zero allocations, zero branch overhead on the hot path.

---

### Zero Hot-Path Heap Allocations
Two explicit pre-allocations in the constructor eliminate every `malloc`/`free` during gameplay:

```cpp
m_particles.reserve(maxParticles);       // particle slots — never reallocate
m_vertices.resize(maxParticles * 4);     // vertex buffer — never resize again
```

Without `reserve()`, `std::vector` doubles capacity when full, triggering `malloc` + bulk `move` + `free` mid-frame — unpredictable latency spikes at the worst possible moment.

---

### Fixed-Timestep Physics (120 Hz, decoupled from render rate)
Physics advances in **fixed 8.3 ms steps** regardless of frame rate:

```cpp
accumulator += realFrameTime;
while (accumulator >= kFixedDt) {   // kFixedDt = 1/120 s
    particles.update(kFixedDt);      // identical trajectory on all machines
    accumulator -= kFixedDt;
}
```

Without this, a 144 Hz machine and a 30 Hz machine would produce different bounce positions and different attractor behaviour from the same input. The `kMaxFrameTime = 0.25 s` clamp prevents the **spiral of death** — if one frame takes 2 s (e.g., debugger pause), the loop is capped at 30 catch-up steps rather than hanging indefinitely.

---

### Parallel Physics & Vertex Build — `std::execution::par_unseq`
Both the physics update loop and the vertex rebuild are parallelised across all CPU cores using C++17 Parallel STL:

```cpp
std::for_each(std::execution::par_unseq,
    m_particles.begin(), m_particles.end(),
    [&](Particle& p) {
        // Each particle owns its own memory — no data race, no locks
        p.acceleration += gravityAccel;
        if (attracting) p.acceleration += attractionForce(p);
        p.update(dt);
        applyBoundary(p);
    }
);
```

Switching between sequential and parallel is **one token** (`PAR_POLICY` macro, set by CMake). The loop body is provably data-race-free: each `Particle&` is a unique object; the only shared mutable state is `m_activeCount`, updated via `fetch_sub(memory_order_relaxed)`.

---

### Semi-Implicit Euler Integration
Each particle carries an `acceleration` accumulator reset every step. The integrator uses **symplectic Euler** (position integrated with the *updated* velocity), which conserves energy — particles orbit rather than spiral:

```
Explicit Euler:      v(t+dt) = v(t) + a·dt          (gains energy → unstable)
                     x(t+dt) = x(t) + v(t)·dt

Semi-implicit Euler: v(t+dt) = v(t) + a·dt          (energy-conserving)
                     x(t+dt) = x(t) + v(t+dt)·dt    ← one reordered line
```

---

### Mouse Attraction with Safe Vector Normalisation
Right-click attract applies a `1/r`-falloff force toward the cursor. Division by zero at the cursor hotspot is prevented with a clamped minimum distance:

```cpp
sf::Vector2f delta    = attractorPos - p.position;
float        dist     = std::sqrt(delta.x*delta.x + delta.y*delta.y);
float        safeDist = std::max(dist, kAttractionMinDist);  // singularity guard
sf::Vector2f dir      = delta / safeDist;                    // unit vector
p.acceleration       += dir * (kAttractionStrength / safeDist);
```

---

### O(1) Active Particle Count — `std::atomic`
Replacing the per-frame O(n) scan with a maintained `std::atomic<size_t>` counter:

| Before | After |
|---|---|
| Loop through all 20,000 slots every frame | Single atomic load: `m_activeCount.load(relaxed)` |
| Called inside `window.setTitle()` at 60 Hz | Counter updated only on birth/death events |
| O(n) string allocs at 60 Hz | Title update rate-limited to 4×/second |

---

### Three Boundary Modes — toggled at runtime
| Key | Mode | Behaviour |
|---|---|---|
| `1` | **None** | Particles fly off-screen and live out their lifetime |
| `2` | **Wrap** | Toroidal topology — exit right, re-enter left (no velocity change) |
| `3` | **Bounce** | Closed box — position reflected, velocity flipped × `kRestitution = 0.65` |

---

## Architecture

```
particle_sim/
├── include/
│   ├── Particle.h          Plain data struct — position, velocity, acceleration,
│   │                       color, lifetime. Owns update(dt): semi-implicit Euler.
│   └── ParticleSystem.h    sf::Drawable manager. Owns the particle + vertex
│                           buffers, RNG, boundary mode, and attractor state.
└── src/
    ├── ParticleSystem.cpp  Physics pipeline, parallel loops, batch vertex build.
    └── main.cpp            Fixed-timestep loop, input, FPS HUD.
```

```
┌─────────────────────────────────┐      ┌──────────────────────────────────────────────────┐
│           Particle              │      │                  ParticleSystem                   │
│         (data struct)           │      │             (extends sf::Drawable)                │
├─────────────────────────────────┤      ├──────────────────────────────────────────────────┤
│ + position     : sf::Vector2f  │      │ - m_particles    : vector<Particle>  (reserved)  │
│ + velocity     : sf::Vector2f  │ 1..* │ - m_vertices     : vector<sf::Vertex>(pre-sized) │
│ + acceleration : sf::Vector2f  │◄─────│ - m_activeCount  : atomic<size_t>               │
│ + color        : sf::Color     │      │ - m_boundaryMode : BoundaryMode                 │
│ + lifetime     : float         │      │ - m_attractorPos : sf::Vector2f                 │
│ + maxLifetime  : float         │      ├──────────────────────────────────────────────────┤
│ + size         : float         │      │ + setEmitter / setBoundaryMode / setAttractor    │
├─────────────────────────────────┤      │ + emit(count)                                   │
│ + update(dt)   — Euler step    │      │ + update(dt)  — parallel physics pipeline       │
│ + isAlive()    : bool          │      │ - rebuildVertices() — parallel vertex write      │
│ + lifeRatio()  : float [0,1]   │      │ - draw()      — one GPU call                    │
└─────────────────────────────────┘      └──────────────────────────────────────────────────┘
```

---

## Getting Started

### Prerequisites

**All platforms:** [CMake 3.20+](https://cmake.org/download/), a C++20 compiler, and Git. SFML is downloaded automatically by CMake — no system install required.

**Linux only** — system headers SFML needs to compile (not runtime libraries):
```bash
# Ubuntu / Debian
sudo apt-get install \
    libgl1-mesa-dev libx11-dev libxrandr-dev \
    libxcursor-dev libudev-dev libfreetype-dev

# Optional — enables std::execution::par_unseq parallel updates
sudo apt-get install libtbb-dev
```

**macOS:** Xcode Command Line Tools supply all required headers:
```bash
xcode-select --install
```

**Windows:** Visual Studio 2022 (any edition) with the "Desktop development with C++" workload. MSVC includes the parallel STL natively — no TBB needed.

---

### Clone & Build

```bash
git clone https://github.com/KyleH777/C--.git
cd C--
```

**Linux / macOS**
```bash
cd particle_sim
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc)
./build/particle_sim
```

**Windows (PowerShell)**
```powershell
cd particle_sim
cmake -B build
cmake --build build --config Release
.\build\Release\particle_sim.exe
```

CMake will print one of these during configuration to tell you which path it took:
```
[particle_sim] Parallel updates: ENABLED (TBB 2021.x)    ← Linux + libtbb-dev
[particle_sim] Parallel updates: ENABLED (MSVC/ConcRT)   ← Windows
[particle_sim] Parallel updates: DISABLED (AppleClang)   ← macOS, sequential
```

---

## Controls

| Input | Action |
|---|---|
| Move mouse | Move particle emitter |
| Left-click (hold) | Burst mode — 3× emission rate |
| Right-click (hold) | Attract all particles toward cursor |
| `1` | Boundary: None (particles leave screen) |
| `2` | Boundary: Wrap (toroidal) |
| `3` | Boundary: Bounce (restitution 0.65) |
| `ESC` | Quit |

---

---

# 🎨 Glitch Art

> A CLI image-processing tool that applies **pixel sorting** and **chromatic aberration** effects to any JPEG or PNG.

![Glitch Art Demo](assets/demo.gif)

[![Glitch Art CI](https://github.com/KyleH777/C--/actions/workflows/glitch_art.yml/badge.svg)](https://github.com/KyleH777/C--/actions/workflows/glitch_art.yml)

## Tech Stack

| Technology | Role |
|---|---|
| **C++20** | Core language |
| **stb_image / stb_image_write** | Header-only image I/O — no external library install |
| **CMake 3.20+** | Build system |

## Key Features

- **Pixel Sort** — rows sorted by luminance, creating the characteristic streaking effect; threshold and direction configurable
- **Chromatic Aberration** — R, G, B channels shifted horizontally by an adjustable pixel offset, simulating lens distortion
- **Header-only dependencies** — `stb_image` is vendored; zero library install steps for contributors
- **Composable effects** — flags can be combined freely; processing order is sort → aberration

## Gallery

| Original | Pixel Sort | Chromatic Aberration | Both Effects |
|:---:|:---:|:---:|:---:|
| ![original](assets/sample_original.png) | ![sorted](assets/sample_sorted.png) | ![aberration](assets/sample_aberration.png) | ![both](assets/sample_both.png) |

## Build & Usage

```bash
cd glitch_art
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc)

# Apply pixel sort only
./build/glitch_art input.jpg output.png --sort

# Apply chromatic aberration (12 px channel shift)
./build/glitch_art input.jpg output.png --aberration 12

# Combine both effects
./build/glitch_art input.jpg output.png --sort --aberration 8
```

---

---

# 📊 Tick Processor

> A high-throughput financial tick-data pipeline — parses large CSV files via **memory-mapped I/O**, distributes work across a **thread-safe MPMC queue**, and scales to available CPU cores automatically.

## Tech Stack

| Technology | Role |
|---|---|
| **C++20** | Core language — `std::span`, `std::atomic`, `std::thread` |
| **POSIX `mmap` / Win32 `MapViewOfFile`** | Zero-copy file reading; the OS page cache serves as the read buffer |
| **CMake 3.20+** | Build system; library / executable / test targets separated |
| **Catch2** (vendored) | Unit tests for parser, queue, and data-processor components |

## Key Features

### Memory-Mapped I/O — zero-copy CSV ingestion
The input file is mapped directly into the process address space. The parser receives a `std::span<const char>` over the mapped region — no `fread`, no intermediate buffer, no heap allocation for file contents. The OS page cache handles I/O scheduling.

### Bounded MPMC Queue with Backpressure
A `ThreadSafeQueue<TickRow>` (mutex + `std::condition_variable`) sits between producers (parse) and consumers (process). The queue capacity is bounded — producers block when the queue is full, providing natural backpressure that prevents memory runaway on slow consumers.

### Producer-Consumer Pipeline
```
┌──────────┐        ┌──────────────────────┐        ┌──────────┐
│ Producer │──push──▶ ThreadSafeQueue<T>    │──pop───▶│ Consumer │
│ (parse)  │        │  bounded, MPMC        │        │  (sink)  │
└──────────┘        └──────────────────────┘        └──────────┘

Multiple producers parse file chunks in parallel.
Multiple consumers drain and run user-defined logic (ConsumerFn).
Thread count auto-scales to std::thread::hardware_concurrency().
```

Producers parse in 256-row batches before acquiring the queue lock — reducing lock contention by ~256×.

### Deadlock-Free Shutdown Ordering
```cpp
queue_.shutdown();   // 1. wake all threads blocked in pop()
for (auto& t : workers_) t.join();  // 2. only then join — no thread can be stuck
```

Joining *before* `shutdown()` would deadlock: workers blocked on an empty queue would never wake. The `WorkerPool` destructor enforces correct ordering as a RAII safety net.

### `std::atomic` Counters — `memory_order_relaxed`
`rows_parsed` and `rows_consumed` use `memory_order_relaxed` — the weakest (and fastest) ordering. This is correct here because the counters are only read for post-run statistics, never used as synchronisation points between operations. On ARM this avoids expensive memory fences.

## Build & Run

```bash
cd tickproc
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc)

# Process a tick-data CSV
./build/tickproc data/ticks.csv

# Run all unit tests
cd build && ctest --output-on-failure
```

---

---

## About

All projects are written in **C++20** and built with **CMake 3.20+**. Each project's CI workflow runs on the same matrix — `ubuntu-latest`, `macos-latest`, `windows-latest` × `Debug` + `Release` — so the build status badges above reflect real cross-platform health.

If you have questions about any design decision or want to discuss the code, feel free to open an issue.
