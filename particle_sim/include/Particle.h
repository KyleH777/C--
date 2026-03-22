#pragma once

#include <SFML/Graphics/Color.hpp>
#include <SFML/System/Vector2.hpp>

// ── Particle ──────────────────────────────────────────────────────────────────
// Plain data struct — no heap allocations, no virtual dispatch.
// Stored by value inside std::vector<Particle> for cache-friendly iteration.
//
// Class diagram (abbreviated):
//
//  ┌──────────────────────────────┐
//  │          Particle            │
//  ├──────────────────────────────┤
//  │ + position    : sf::Vector2f │
//  │ + velocity    : sf::Vector2f │
//  │ + color       : sf::Color    │
//  │ + lifetime    : float        │  ← seconds remaining
//  │ + maxLifetime : float        │  ← original duration (for ratio)
//  │ + size        : float        │  ← half-side of rendered quad (px)
//  ├──────────────────────────────┤
//  │ + isAlive()   : bool         │
//  │ + lifeRatio() : float [0,1]  │  ← 1 = just born, 0 = dead
//  └──────────────────────────────┘

struct Particle {
    sf::Vector2f position{};
    sf::Vector2f velocity{};
    sf::Color    color   = sf::Color::White;
    float        lifetime    = 0.f;   // seconds remaining
    float        maxLifetime = 1.f;   // original lifetime
    float        size        = 4.f;   // half-width of quad in pixels

    [[nodiscard]] bool  isAlive()   const noexcept { return lifetime > 0.f; }

    // Returns a ratio in [0, 1]: 1.0 = freshly spawned, 0.0 = just expired.
    // Used to drive color and size interpolation without any extra state.
    [[nodiscard]] float lifeRatio() const noexcept { return lifetime / maxLifetime; }
};
