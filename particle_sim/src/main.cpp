#include <algorithm>
#include <string>
#include <string_view>

#include <SFML/Graphics.hpp>

#include "ParticleSystem.h"

// ── Fixed Timestep Constants ──────────────────────────────────────────────────
//
// WHY fixed timestep?
//
// Variable-dt problem:
//   On a fast machine (144 Hz) dt ≈ 0.007 s  → particles move 7 mm/step.
//   On a slow machine ( 30 Hz) dt ≈ 0.033 s  → particles move 33 mm/step.
//   Same simulation, different trajectories. Bounces happen at different
//   positions, attraction behaves differently — physics is non-deterministic.
//
// Fixed-timestep solution (Gaffer on Games "Fix Your Timestep"):
//   Physics always advances in steps of kFixedDt (e.g. 1/120 s = 8.3 ms).
//   The render loop runs at whatever rate the GPU allows.
//   An "accumulator" absorbs the mismatch:
//
//     accumulator += realFrameTime;
//     while (accumulator >= kFixedDt) {
//         simulate(kFixedDt);           // deterministic, same on every machine
//         accumulator -= kFixedDt;
//     }
//     render();
//
//   Result: physics is identical at 30 Hz, 60 Hz, 144 Hz, and under debugger.
//
// Spiral-of-death guard:
//   If a single frame takes 2 s (e.g. breakpoint hit), accumulator = 2.0.
//   The while loop would run 240 iterations, making the NEXT frame take even
//   longer → more iterations → infinite loop, program hangs.
//   Clamping frameTime to kMaxFrameTime (0.25 s) limits catch-up to 30 steps.
//
static constexpr double kFixedDt      = 1.0 / 120.0;  // 120 Hz physics
static constexpr double kMaxFrameTime = 0.25;          // spiral-of-death guard

namespace {
    constexpr unsigned    kWidth        = 1280;
    constexpr unsigned    kHeight       = 720;
    constexpr std::size_t kMaxParticles = 20'000;
    constexpr std::size_t kEmitPerFrame = 25;
    constexpr std::size_t kBurstPerFrame= 80;

    std::string_view modeName(BoundaryMode m) noexcept {
        switch (m) {
            case BoundaryMode::None:   return "None";
            case BoundaryMode::Wrap:   return "Wrap";
            case BoundaryMode::Bounce: return "Bounce";
        }
        return "?";
    }
}

int main() {
    sf::RenderWindow window(
        sf::VideoMode(kWidth, kHeight),
        "Particle Simulation",
        sf::Style::Close
    );
    window.setFramerateLimit(60);

    ParticleSystem particles(kMaxParticles,
                             static_cast<float>(kWidth),
                             static_cast<float>(kHeight));
    particles.setEmitter({ kWidth / 2.f, kHeight / 2.f });

    // ── Fixed timestep state ──────────────────────────────────────────────────
    sf::Clock wallClock;
    double    accumulator  = 0.0;
    double    previousTime = wallClock.getElapsedTime().asSeconds();

    // ── FPS / HUD state ───────────────────────────────────────────────────────
    // Title bar is updated at most 4× per second — avoids string allocation at 60 Hz.
    float    fpsAccum     = 0.f;
    unsigned fpsFrames    = 0;
    int      displayedFps = 0;

    while (window.isOpen()) {

        // ── 1. Events ─────────────────────────────────────────────────────────
        sf::Event event;
        while (window.pollEvent(event)) {
            if (event.type == sf::Event::Closed) window.close();
            if (event.type == sf::Event::KeyPressed) {
                switch (event.key.code) {
                    case sf::Keyboard::Escape: window.close();                        break;
                    case sf::Keyboard::Num1:   particles.setBoundaryMode(BoundaryMode::None);   break;
                    case sf::Keyboard::Num2:   particles.setBoundaryMode(BoundaryMode::Wrap);   break;
                    case sf::Keyboard::Num3:   particles.setBoundaryMode(BoundaryMode::Bounce); break;
                    default: break;
                }
            }
        }

        // ── 2. Wall-clock time ────────────────────────────────────────────────
        const double currentTime = wallClock.getElapsedTime().asSeconds();
        const double frameTime   = std::min(currentTime - previousTime, kMaxFrameTime);
        previousTime = currentTime;

        // ── 3. Input snapshot (once per render frame, not per physics step) ───
        const sf::Vector2i mouseI = sf::Mouse::getPosition(window);
        const sf::Vector2f mouseF = { static_cast<float>(mouseI.x),
                                      static_cast<float>(mouseI.y) };
        particles.setEmitter(mouseF);

        const std::size_t toEmit   = sf::Mouse::isButtonPressed(sf::Mouse::Left)
                                     ? kBurstPerFrame : kEmitPerFrame;
        const bool        attract  = sf::Mouse::isButtonPressed(sf::Mouse::Right);
        particles.setAttractor(mouseF, attract);

        // ── 4. Fixed-timestep physics loop ────────────────────────────────────
        //
        // Emit once per render frame (not per physics step) to keep the
        // emission rate independent of kFixedDt. Physics can run 1 or 2 steps
        // per render frame; we don't want to double the particle count.
        particles.emit(toEmit);

        accumulator += frameTime;
        while (accumulator >= kFixedDt) {
            particles.update(static_cast<float>(kFixedDt));
            accumulator -= kFixedDt;
        }

        // ── 5. Render ─────────────────────────────────────────────────────────
        window.clear(sf::Color(8, 8, 18));
        window.draw(particles);
        window.display();

        // ── 6. HUD — rate-limited to 4× / second ──────────────────────────────
        // String allocation + setTitle() are expensive. Doing them every frame
        // at 60 Hz wastes ~0.5 ms/frame. Updating 4×/s is invisible to the eye.
        fpsAccum += static_cast<float>(frameTime);
        ++fpsFrames;
        if (fpsAccum >= 0.25f) {
            displayedFps = static_cast<int>(static_cast<float>(fpsFrames) / fpsAccum);
            fpsAccum  = 0.f;
            fpsFrames = 0;

            window.setTitle(
                "Particle Sim"
                "  |  active: "  + std::to_string(particles.activeCount()) +
                " / "            + std::to_string(particles.maxCapacity()) +
                "  |  FPS: "     + std::to_string(displayedFps) +
                "  |  "          + std::string(modeName(particles.boundaryMode())) +
                "  |  [1]None [2]Wrap [3]Bounce  LMB:burst  RMB:attract"
            );
        }
    }

    return 0;
}
