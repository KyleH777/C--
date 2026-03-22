#pragma once

#include <cstddef>
#include <random>
#include <vector>

#include <SFML/Graphics.hpp>

#include "Particle.h"

// ── ParticleSystem ────────────────────────────────────────────────────────────
// Manages the full lifecycle of up to `maxParticles` particles.
//
// Class diagram:
//
//  ┌──────────────────────────────────────────────────────────────┐
//  │                      ParticleSystem                          │
//  │                   (extends sf::Drawable)                     │
//  ├──────────────────────────────────────────────────────────────┤
//  │ - m_emitter      : sf::Vector2f                              │
//  │ - m_particles    : std::vector<Particle>  ← pre-reserved     │
//  │ - m_vertices     : sf::VertexArray (Quads) ← batch rendering │
//  │ - m_maxParticles : const std::size_t                         │
//  │ - m_rng          : std::mt19937                              │
//  ├──────────────────────────────────────────────────────────────┤
//  │ + ParticleSystem(maxParticles : size_t)                      │
//  │ + setEmitter(pos : sf::Vector2f) : void                      │
//  │ + emit(count : size_t)           : void                      │
//  │ + update(dt : float)             : void                      │
//  │ + activeCount()                  : size_t                    │
//  │ + maxCapacity()                  : size_t                    │
//  ├──────────────────────────────────────────────────────────────┤
//  │ - draw(target, states)           : void  [sf::Drawable]      │
//  │ - resetParticle(p)               : void                      │
//  │ - rebuildVertices()              : void                      │
//  └──────────────────────────────────────────────────────────────┘
//
// Inherits sf::Drawable so callers can simply write:
//   window.draw(particleSystem);
//
// Performance contract:
//   - reserve() called once in the constructor — zero heap allocations
//     on the simulation hot-path thereafter.
//   - sf::VertexArray with sf::Quads means one GPU draw call per frame,
//     regardless of particle count.
//   - Dead slots are reused without erase() to avoid O(n) shifts.

class ParticleSystem : public sf::Drawable {
public:
    /// Pre-allocates storage for maxParticles. After construction, emit() and
    /// update() never trigger a heap allocation.
    explicit ParticleSystem(std::size_t maxParticles);

    /// Move the spawn origin. Call once per frame before emit().
    void setEmitter(sf::Vector2f pos) noexcept;

    /// Spawn up to `count` particles at the current emitter position.
    void emit(std::size_t count);

    /// Advance the simulation by dt seconds.
    void update(float dt);

    [[nodiscard]] std::size_t activeCount() const noexcept;
    [[nodiscard]] std::size_t maxCapacity()  const noexcept;

private:
    // Called by window.draw(*this) — submits m_vertices in one draw call.
    void draw(sf::RenderTarget& target, sf::RenderStates states) const override;

    // Reinitialise a dead (or freshly pushed) particle at m_emitter.
    void resetParticle(Particle& p);

    // Rebuild m_vertices from m_particles every frame (O(n), cache-friendly).
    void rebuildVertices();

    sf::Vector2f          m_emitter{};
    std::vector<Particle> m_particles;   // contiguous block — see reserve()
    sf::VertexArray       m_vertices;    // sf::Quads — one draw call always
    const std::size_t     m_maxParticles;

    // A single RNG per ParticleSystem, seeded once from hardware entropy.
    // std::mt19937 is fast and has good statistical properties for visual work.
    std::mt19937                          m_rng{ std::random_device{}() };
    std::uniform_real_distribution<float> m_angleDist{ 0.f, 6.28318f };
    std::uniform_real_distribution<float> m_speedDist{ 80.f, 280.f   };
    std::uniform_real_distribution<float> m_lifeDist { 0.8f, 2.4f    };
    std::uniform_real_distribution<float> m_angleBias{-0.4f, 0.4f    };
};
