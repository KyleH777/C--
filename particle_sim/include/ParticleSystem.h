#pragma once

#include <cstddef>
#include <random>
#include <vector>

#include <SFML/Graphics.hpp>

#include "Particle.h"

// ── BoundaryMode ──────────────────────────────────────────────────────────────
// Controls what happens when a particle exits the window rectangle.
//
//  None   — no intervention; particles fly off-screen and live out their life.
//
//  Wrap   — teleport to the opposite edge (toroidal topology):
//             x < 0     →  x += width
//             x > width →  x -= width        (same for y)
//           Gives an infinite-space feel; good for ambient simulations.
//
//  Bounce — reflect velocity component and apply restitution (energy loss):
//             if x < 0:   x = -x;          vx = +|vx| * kRestitution
//             if x > W:   x = 2W - x;      vx = -|vx| * kRestitution
//           Particles stay on-screen; good for enclosed chamber simulations.
//
enum class BoundaryMode { None, Wrap, Bounce };

// ── ParticleSystem ────────────────────────────────────────────────────────────
// Manages the full lifecycle of up to `maxParticles` particles.
//
// Updated class diagram:
//
//  ┌──────────────────────────────────────────────────────────────────┐
//  │                       ParticleSystem                             │
//  │                    (extends sf::Drawable)                        │
//  ├──────────────────────────────────────────────────────────────────┤
//  │ - m_emitter        : sf::Vector2f                                │
//  │ - m_particles      : std::vector<Particle>   ← pre-reserved      │
//  │ - m_vertices       : sf::VertexArray (Quads) ← batch rendering   │
//  │ - m_maxParticles   : const std::size_t                           │
//  │ - m_boundaryMode   : BoundaryMode                                │
//  │ - m_boundsW/H      : float                   ← window dimensions │
//  │ - m_attractorPos   : sf::Vector2f            ← mouse world pos   │
//  │ - m_attractorActive: bool                                        │
//  │ - m_rng            : std::mt19937                                │
//  ├──────────────────────────────────────────────────────────────────┤
//  │ + ParticleSystem(maxParticles, boundsW, boundsH)                 │
//  │ + setEmitter(pos)                                                │
//  │ + emit(count)                                                    │
//  │ + update(dt)           ← physics pipeline (see .cpp)            │
//  │ + setBoundaryMode(mode)                                          │
//  │ + setAttractor(pos, active)                                      │
//  │ + activeCount() / maxCapacity()                                  │
//  ├──────────────────────────────────────────────────────────────────┤
//  │ - draw(target, states)       [sf::Drawable override]             │
//  │ - resetParticle(p)                                               │
//  │ - applyBoundary(p)                                               │
//  │ - rebuildVertices()                                              │
//  └──────────────────────────────────────────────────────────────────┘

class ParticleSystem : public sf::Drawable {
public:
    /// Pre-allocates storage for maxParticles and records window dimensions
    /// for boundary calculations.
    explicit ParticleSystem(std::size_t maxParticles,
                            float boundsW, float boundsH);

    /// Move the spawn origin. Call once per frame before emit().
    void setEmitter(sf::Vector2f pos) noexcept;

    /// Spawn up to `count` particles at the current emitter position.
    void emit(std::size_t count);

    /// Advance the simulation by dt seconds.
    /// Physics pipeline per particle:
    ///   1. Accumulate gravity into acceleration
    ///   2. If attractor is active: accumulate attraction force
    ///   3. p.update(dt)   — semi-implicit Euler, resets acceleration
    ///   4. applyBoundary  — wrap or bounce
    ///   5. Visual decay   — colour + size from lifeRatio()
    void update(float dt);

    /// Choose what happens when particles hit the window edge.
    void setBoundaryMode(BoundaryMode mode) noexcept;

    /// Set the attractor (mouse cursor world position) and whether it is active.
    /// When active, every particle gains an acceleration toward `pos` each frame,
    /// scaled by 1/distance so close particles feel it more strongly.
    void setAttractor(sf::Vector2f pos, bool active) noexcept;

    [[nodiscard]] std::size_t  activeCount()   const noexcept;
    [[nodiscard]] std::size_t  maxCapacity()   const noexcept;
    [[nodiscard]] BoundaryMode boundaryMode()  const noexcept;

private:
    void draw(sf::RenderTarget& target, sf::RenderStates states) const override;
    void resetParticle(Particle& p);
    void applyBoundary(Particle& p) const noexcept;
    void rebuildVertices();

    sf::Vector2f          m_emitter{};
    std::vector<Particle> m_particles;
    sf::VertexArray       m_vertices;
    const std::size_t     m_maxParticles;

    BoundaryMode          m_boundaryMode   = BoundaryMode::None;
    float                 m_boundsW        = 1280.f;
    float                 m_boundsH        = 720.f;

    sf::Vector2f          m_attractorPos{};
    bool                  m_attractorActive = false;

    std::mt19937                          m_rng{ std::random_device{}() };
    std::uniform_real_distribution<float> m_angleDist{ 0.f, 6.28318f };
    std::uniform_real_distribution<float> m_speedDist{ 80.f, 280.f   };
    std::uniform_real_distribution<float> m_lifeDist { 0.8f, 2.4f    };
    std::uniform_real_distribution<float> m_angleBias{-0.4f, 0.4f    };
};
