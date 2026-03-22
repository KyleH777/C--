#include <string>

#include <SFML/Graphics.hpp>

#include "ParticleSystem.h"

int main() {
    // ── Window ────────────────────────────────────────────────────────────────
    constexpr unsigned kWidth  = 1280;
    constexpr unsigned kHeight = 720;

    sf::RenderWindow window(
        sf::VideoMode(kWidth, kHeight),
        "Particle Simulation  |  Left-click: burst  |  ESC: quit",
        sf::Style::Close
    );
    window.setFramerateLimit(60);

    // ── Particle system ───────────────────────────────────────────────────────
    // 20 000 slots reserved once — zero allocations during the game loop.
    constexpr std::size_t kMaxParticles  = 20'000;
    constexpr std::size_t kEmitPerFrame  = 25;   // normal emission rate
    constexpr std::size_t kBurstPerFrame = 80;   // on left-click hold

    ParticleSystem particles(kMaxParticles);

    // Start the emitter in the window centre so something is visible immediately.
    particles.setEmitter({ kWidth / 2.f, kHeight / 2.f });

    sf::Clock clock;
    unsigned  frameCount    = 0;
    float     fpsAccum      = 0.f;
    float     displayedFps  = 0.f;

    // ── Main loop ─────────────────────────────────────────────────────────────
    while (window.isOpen()) {

        // ── 1. Events ─────────────────────────────────────────────────────────
        sf::Event event;
        while (window.pollEvent(event)) {
            if (event.type == sf::Event::Closed)
                window.close();
            if (event.type == sf::Event::KeyPressed &&
                event.key.code == sf::Keyboard::Escape)
                window.close();
        }

        // ── 2. Timing ─────────────────────────────────────────────────────────
        const float dt = clock.restart().asSeconds();

        fpsAccum += dt;
        ++frameCount;
        if (fpsAccum >= 0.25f) {          // update title 4× / second
            displayedFps = static_cast<float>(frameCount) / fpsAccum;
            fpsAccum     = 0.f;
            frameCount   = 0;
        }

        // ── 3. Emit ───────────────────────────────────────────────────────────
        // Track the mouse every frame so the fountain follows the cursor.
        const sf::Vector2i mouse = sf::Mouse::getPosition(window);
        particles.setEmitter({
            static_cast<float>(mouse.x),
            static_cast<float>(mouse.y)
        });

        const std::size_t toEmit = sf::Mouse::isButtonPressed(sf::Mouse::Left)
                                   ? kBurstPerFrame
                                   : kEmitPerFrame;
        particles.emit(toEmit);

        // ── 4. Update ─────────────────────────────────────────────────────────
        particles.update(dt);

        // ── 5. Render ─────────────────────────────────────────────────────────
        window.clear(sf::Color(8, 8, 18));   // deep-navy background
        window.draw(particles);              // one sf::VertexArray draw call
        window.display();

        // ── 6. HUD (title bar) ────────────────────────────────────────────────
        window.setTitle(
            "Particle Sim  |  active: "    + std::to_string(particles.activeCount()) +
            " / "                           + std::to_string(particles.maxCapacity()) +
            "  |  FPS: "                    + std::to_string(static_cast<int>(displayedFps))
        );
    }

    return 0;
}
