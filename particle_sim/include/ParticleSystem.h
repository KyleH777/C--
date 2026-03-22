#pragma once

#include <atomic>
#include <cstddef>
#include <random>
#include <vector>

#include <SFML/Graphics.hpp>

#include "Particle.h"

// ── BoundaryMode ──────────────────────────────────────────────────────────────
enum class BoundaryMode { None, Wrap, Bounce };

// ── ParticleSystem ────────────────────────────────────────────────────────────
//
// PERFORMANCE ARCHITECTURE — four layers, each independently important:
//
// ┌─────────────────────────────────────────────────────────────────────────┐
// │ Layer 1 — Memory layout                                                 │
// │   std::vector<Particle>   reserved once → zero heap allocs hot-path     │
// │   std::vector<sf::Vertex> resized  once → zero resize calls per frame   │
// │   Both vectors are contiguous blocks: CPU prefetcher streams linearly.  │
// ├─────────────────────────────────────────────────────────────────────────┤
// │ Layer 2 — CPU parallelism                                               │
// │   std::for_each(execution::par_unseq, …) in update() + rebuildVerts()  │
// │   Each particle is independent → embarrassingly parallel, no locks.     │
// │   Guarded by PARTICLE_PARALLEL (set in CMakeLists when TBB available).  │
// ├─────────────────────────────────────────────────────────────────────────┤
// │ Layer 3 — GPU batch rendering                                           │
// │   One draw call submits all vertices via the raw sf::Vertex* overload.  │
// │   Dead particles write transparent quads — GPU discards them for free.  │
// │   "Naive" approach (one CircleShape per particle) = N draw calls,       │
// │   N OpenGL state changes: 10–100× slower for N > 1 000.                │
// ├─────────────────────────────────────────────────────────────────────────┤
// │ Layer 4 — Zero per-frame allocations                                    │
// │   std::atomic activeCount → eliminates O(n) scan every frame            │
// │   Title bar updated only on FPS tick (4×/s) → no string allocs at 60Hz │
// └─────────────────────────────────────────────────────────────────────────┘

class ParticleSystem : public sf::Drawable {
public:
    explicit ParticleSystem(std::size_t maxParticles,
                            float boundsW, float boundsH);

    void setEmitter(sf::Vector2f pos)              noexcept;
    void setBoundaryMode(BoundaryMode mode)        noexcept;
    void setAttractor(sf::Vector2f pos, bool active) noexcept;

    void emit(std::size_t count);

    /// Full physics + render-data pipeline. Fixed-timestep-safe: caller
    /// controls how many times per rendered frame this is called.
    void update(float dt);

    // O(1) — maintained by atomic counter, not recomputed each call.
    [[nodiscard]] std::size_t  activeCount()  const noexcept;
    [[nodiscard]] std::size_t  maxCapacity()  const noexcept;
    [[nodiscard]] BoundaryMode boundaryMode() const noexcept;

private:
    void draw(sf::RenderTarget& target, sf::RenderStates states) const override;
    void resetParticle(Particle& p);
    void applyBoundary(Particle& p)   const noexcept;
    void rebuildVertices();

    sf::Vector2f          m_emitter{};
    std::vector<Particle> m_particles;

    // Raw vertex buffer — gives us reserve(), data(), and the raw draw() overload.
    // sf::VertexArray is a thin wrapper that lacks reserve(); using std::vector
    // directly lets us pre-allocate the full maxParticles * 4 block once.
    std::vector<sf::Vertex> m_vertices;

    const std::size_t     m_maxParticles;

    BoundaryMode          m_boundaryMode    = BoundaryMode::None;
    float                 m_boundsW         = 1280.f;
    float                 m_boundsH         = 720.f;

    sf::Vector2f          m_attractorPos{};
    bool                  m_attractorActive = false;

    // Maintained with atomic ±1 ops — activeCount() costs one load, not O(n).
    std::atomic<std::size_t> m_activeCount{0};

    std::mt19937                          m_rng{ std::random_device{}() };
    std::uniform_real_distribution<float> m_angleDist{ 0.f, 6.28318f };
    std::uniform_real_distribution<float> m_speedDist{ 80.f, 280.f   };
    std::uniform_real_distribution<float> m_lifeDist { 0.8f, 2.4f    };
    std::uniform_real_distribution<float> m_angleBias{-0.4f, 0.4f    };
};
