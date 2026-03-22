#include <string>
#include <string_view>

#include <SFML/Graphics.hpp>

#include "ParticleSystem.h"

namespace {
    constexpr unsigned    kWidth        = 1280;
    constexpr unsigned    kHeight       = 720;
    constexpr std::size_t kMaxParticles = 20'000;
    constexpr std::size_t kEmitPerFrame = 25;
    constexpr std::size_t kBurstPerFrame= 80;

    std::string_view modeName(BoundaryMode m) {
        switch (m) {
            case BoundaryMode::None:   return "None";
            case BoundaryMode::Wrap:   return "Wrap";
            case BoundaryMode::Bounce: return "Bounce";
        }
        return "?";
    }
}

int main() {
    // ── Window ────────────────────────────────────────────────────────────────
    sf::RenderWindow window(
        sf::VideoMode(kWidth, kHeight),
        "Particle Simulation",
        sf::Style::Close
    );
    window.setFramerateLimit(60);

    // ── Particle system ───────────────────────────────────────────────────────
    ParticleSystem particles(kMaxParticles,
                             static_cast<float>(kWidth),
                             static_cast<float>(kHeight));

    particles.setEmitter({ kWidth / 2.f, kHeight / 2.f });

    sf::Clock clock;
    unsigned  frameCount   = 0;
    float     fpsAccum     = 0.f;
    float     displayedFps = 0.f;

    // ── Main loop ─────────────────────────────────────────────────────────────
    while (window.isOpen()) {

        // ── 1. Events ─────────────────────────────────────────────────────────
        sf::Event event;
        while (window.pollEvent(event)) {
            if (event.type == sf::Event::Closed)
                window.close();

            if (event.type == sf::Event::KeyPressed) {
                switch (event.key.code) {
                    case sf::Keyboard::Escape:
                        window.close();
                        break;

                    // ── Boundary mode ──────────────────────────────────────
                    // Key '1' — no boundary: particles fly off-screen.
                    case sf::Keyboard::Num1:
                        particles.setBoundaryMode(BoundaryMode::None);
                        break;

                    // Key '2' — wrap: exit left → re-enter right (toroidal).
                    case sf::Keyboard::Num2:
                        particles.setBoundaryMode(BoundaryMode::Wrap);
                        break;

                    // Key '3' — bounce: reflect + lose 35% energy per wall hit.
                    case sf::Keyboard::Num3:
                        particles.setBoundaryMode(BoundaryMode::Bounce);
                        break;

                    default: break;
                }
            }
        }

        // ── 2. Timing ─────────────────────────────────────────────────────────
        const float dt = clock.restart().asSeconds();

        fpsAccum += dt;
        ++frameCount;
        if (fpsAccum >= 0.25f) {
            displayedFps = static_cast<float>(frameCount) / fpsAccum;
            fpsAccum     = 0.f;
            frameCount   = 0;
        }

        // ── 3. Input → system state ───────────────────────────────────────────
        const sf::Vector2i mouseI  = sf::Mouse::getPosition(window);
        const sf::Vector2f mouseF  = { static_cast<float>(mouseI.x),
                                       static_cast<float>(mouseI.y) };

        // Emitter always follows the cursor.
        particles.setEmitter(mouseF);

        // Left-click: burst emission.
        const std::size_t toEmit = sf::Mouse::isButtonPressed(sf::Mouse::Left)
                                   ? kBurstPerFrame
                                   : kEmitPerFrame;
        particles.emit(toEmit);

        // ── Mouse Attraction (right-click hold) ───────────────────────────────
        //
        // While the right mouse button is held, every particle gains an
        // acceleration toward the cursor.  The direction vector is computed
        // inside ParticleSystem::update() using the position stored here.
        //
        // We expose two orthogonal concerns to the system:
        //   • WHERE the attractor is  (mouseF — set every frame regardless)
        //   • WHETHER it is active    (right button state)
        //
        // This means main.cpp stays free of physics math — it only passes
        // intent; the system decides how to translate that into acceleration.
        //
        const bool attracting = sf::Mouse::isButtonPressed(sf::Mouse::Right);
        particles.setAttractor(mouseF, attracting);

        // ── 4. Update ─────────────────────────────────────────────────────────
        particles.update(dt);

        // ── 5. Render ─────────────────────────────────────────────────────────
        window.clear(sf::Color(8, 8, 18));
        window.draw(particles);
        window.display();

        // ── 6. HUD ────────────────────────────────────────────────────────────
        window.setTitle(
            "Particle Sim"
            "  |  active: "  + std::to_string(particles.activeCount()) +
            " / "            + std::to_string(particles.maxCapacity()) +
            "  |  FPS: "     + std::to_string(static_cast<int>(displayedFps)) +
            "  |  Boundary: "+ std::string(modeName(particles.boundaryMode())) +
            "  |  [1]None [2]Wrap [3]Bounce  LMB:burst  RMB:attract"
        );
    }

    return 0;
}
