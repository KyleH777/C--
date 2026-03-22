#pragma once

#include <SFML/Graphics/Color.hpp>
#include <SFML/System/Vector2.hpp>

// ── Particle ──────────────────────────────────────────────────────────────────
// Plain data struct — no heap allocations, no virtual dispatch.
// Stored by value inside std::vector<Particle> for cache-friendly iteration.
//
// Updated class diagram:
//
//  ┌──────────────────────────────────┐
//  │             Particle             │
//  ├──────────────────────────────────┤
//  │ + position     : sf::Vector2f   │
//  │ + velocity     : sf::Vector2f   │
//  │ + acceleration : sf::Vector2f   │  ← accumulates forces, reset each step
//  │ + color        : sf::Color      │
//  │ + lifetime     : float          │  ← seconds remaining
//  │ + maxLifetime  : float          │  ← original duration (for ratio)
//  │ + size         : float          │  ← half-side of rendered quad (px)
//  ├──────────────────────────────────┤
//  │ + update(dt : float) : void     │  ← semi-implicit Euler integration
//  │ + isAlive()          : bool     │
//  │ + lifeRatio()        : float    │  ← [0, 1], drives color + size decay
//  └──────────────────────────────────┘

struct Particle {
    sf::Vector2f position{};
    sf::Vector2f velocity{};
    sf::Vector2f acceleration{};      // net force accumulator (mass = 1)
    sf::Color    color       = sf::Color::White;
    float        lifetime    = 0.f;   // seconds remaining
    float        maxLifetime = 1.f;   // original lifetime
    float        size        = 4.f;   // half-width of quad in pixels

    // ── Euler Integration ────────────────────────────────────────────────────
    //
    // Newton's 2nd law:  F = m·a  →  a = F/m
    // We treat mass = 1, so  acceleration ≡ net force.
    //
    // Each frame, external code adds forces directly to `acceleration`:
    //   p.acceleration += gravityAccel;
    //   p.acceleration += attractionForce;
    //   …
    //
    // Then update(dt) integrates and resets:
    //
    //   Explicit (Forward) Euler — uses state at START of step:
    //     v(t+dt) = v(t) + a(t)·dt
    //     x(t+dt) = x(t) + v(t)·dt        ← position uses old velocity
    //
    //   Semi-Implicit (Symplectic) Euler — uses NEW velocity for position:
    //     v(t+dt) = v(t) + a(t)·dt
    //     x(t+dt) = x(t) + v(t+dt)·dt     ← position uses updated velocity
    //
    // We use semi-implicit Euler: it costs nothing extra (one reordered line)
    // but conserves energy much better than explicit Euler. Planets orbit
    // instead of spiralling; bouncing balls don't gain energy over time.
    //
    void update(float dt) noexcept {
        velocity     += acceleration * dt;   // step 1: integrate velocity
        position     += velocity     * dt;   // step 2: integrate position (uses v(t+dt))
        acceleration  = {};                  // step 3: clear — forces re-applied next frame
        lifetime     -= dt;
    }

    [[nodiscard]] bool  isAlive()   const noexcept { return lifetime > 0.f; }

    // Returns [1 → 0] over the particle's lifetime. Used to interpolate
    // colour and size without storing any extra per-particle state.
    [[nodiscard]] float lifeRatio() const noexcept { return lifetime / maxLifetime; }
};
